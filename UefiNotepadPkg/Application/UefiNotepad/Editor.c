/** @file
  Editing operations: cursor movement, selection, clipboard, undo/redo,
  find and replace.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

EDITOR  gEd;

STATIC
POS
MkPos (
  UINTN  Line,
  UINTN  Col
  )
{
  POS  P;

  P.Line = Line;
  P.Col  = Col;
  return P;
}

STATIC
LINE *
CurLine (
  VOID
  )
{
  return &gEd.Doc.Lines[gEd.Cursor.Line];
}

VOID
EdSetMessage (
  CONST CHAR16  *Msg
  )
{
  StrnCpyS (gEd.Message, ARRAY_SIZE (gEd.Message), Msg, ARRAY_SIZE (gEd.Message) - 1);
}

//
// ---------------- Undo stacks ----------------
//
STATIC
VOID
StackClear (
  UNDO_STACK  *S
  )
{
  UINTN  I;

  for (I = 0; I < S->Count; I++) {
    if (S->Items[I].Text != NULL) {
      FreePool (S->Items[I].Text);
    }
  }

  S->Count = 0;
}

STATIC
VOID
StackPush (
  UNDO_STACK  *S,
  UNDO_REC    *R
  )
{
  UNDO_REC  *N;
  UINTN     NewCap;

  if (S->Count == S->Cap) {
    NewCap = (S->Cap == 0) ? 64 : S->Cap * 2;
    N      = AllocateZeroPool (NewCap * sizeof (UNDO_REC));
    if (N == NULL) {
      return;
    }

    if (S->Items != NULL) {
      CopyMem (N, S->Items, S->Count * sizeof (UNDO_REC));
      FreePool (S->Items);
    }

    S->Items = N;
    S->Cap   = NewCap;
  }

  S->Items[S->Count++] = *R;
}

VOID
EdClearUndo (
  VOID
  )
{
  StackClear (&gEd.Undo);
  StackClear (&gEd.Redo);
  gEd.Group       = 1;
  gEd.CanCoalesce = FALSE;
}

VOID
EdNewGroup (
  VOID
  )
{
  gEd.Group++;
  gEd.CanCoalesce = FALSE;
}

STATIC
VOID
Touched (
  VOID
  )
{
  gEd.Modified     = TRUE;
  gEd.MaxColsDirty = TRUE;
  gEd.WrapDirty    = TRUE;
}

/**
  Insert text at At and record it. Returns the end position.
**/
STATIC
POS
RecInsert (
  POS           At,
  CONST CHAR16  *Str,
  UINTN         Len
  )
{
  UNDO_REC  R;
  UNDO_REC  *Top;
  POS       End;
  CHAR16    *Joined;

  DocInsert (&gEd.Doc, At, Str, Len, &End);
  StackClear (&gEd.Redo);
  Touched ();

  //
  // Join with the previous insert of the same group when contiguous.
  //
  if (gEd.Undo.Count > 0) {
    Top = &gEd.Undo.Items[gEd.Undo.Count - 1];
    if ((Top->Type == UNDO_INSERT) && (Top->Group == gEd.Group) &&
        (PosCmp (PosAdvance (Top->At, Top->Text, Top->Len), At) == 0))
    {
      Joined = AllocatePool ((Top->Len + Len) * sizeof (CHAR16));
      if (Joined != NULL) {
        CopyMem (Joined, Top->Text, Top->Len * sizeof (CHAR16));
        CopyMem (Joined + Top->Len, Str, Len * sizeof (CHAR16));
        FreePool (Top->Text);
        Top->Text = Joined;
        Top->Len += Len;
        return End;
      }
    }
  }

  ZeroMem (&R, sizeof (R));
  R.Type         = UNDO_INSERT;
  R.At           = At;
  R.Text         = AllocateCopyPool ((Len + 1) * sizeof (CHAR16), Str);
  R.Len          = Len;
  R.Group        = gEd.Group;
  R.CursorBefore = gEd.Cursor;
  StackPush (&gEd.Undo, &R);
  return End;
}

STATIC
VOID
RecDelete (
  POS  S,
  POS  E
  )
{
  UNDO_REC  R;
  UINTN     Len;

  if (PosCmp (S, E) == 0) {
    return;
  }

  if (PosCmp (S, E) > 0) {
    POS  T;

    T = S;
    S = E;
    E = T;
  }

  ZeroMem (&R, sizeof (R));
  R.Type         = UNDO_DELETE;
  R.At           = S;
  R.Text         = DocGetText (&gEd.Doc, S, E, &Len);
  R.Len          = Len;
  R.Group        = gEd.Group;
  R.CursorBefore = gEd.Cursor;
  DocDelete (&gEd.Doc, S, E);
  StackClear (&gEd.Redo);
  StackPush (&gEd.Undo, &R);
  Touched ();
}

//
// ---------------- Lifetime ----------------
//
VOID
EdInit (
  VOID
  )
{
  ZeroMem (&gEd, sizeof (gEd));
  DocInit (&gEd.Doc);
  gEd.Enc          = ENC_UTF8;
  gEd.Eol          = EOL_CRLF;
  gEd.ShowStatus   = TRUE;
  gEd.OpenMenu     = -1;
  gEd.MenuHot      = -1;
  gEd.Group        = 1;
  gEd.MaxColsDirty = TRUE;
  gEd.WrapDirty    = TRUE;
  gEd.WordWrap     = TRUE;
  gEd.SubOpen      = -1;
  gEd.SubHot       = -1;
  gEd.CaretOn      = TRUE;
}

VOID
EdFree (
  VOID
  )
{
  StackClear (&gEd.Undo);
  StackClear (&gEd.Redo);
  if (gEd.Undo.Items != NULL) {
    FreePool (gEd.Undo.Items);
  }

  if (gEd.Redo.Items != NULL) {
    FreePool (gEd.Redo.Items);
  }

  if (gEd.Clip != NULL) {
    FreePool (gEd.Clip);
  }

  DocFree (&gEd.Doc);
}

//
// ---------------- Selection ----------------
//
BOOLEAN
EdHasSel (
  VOID
  )
{
  return PosCmp (gEd.Cursor, gEd.Anchor) != 0;
}

VOID
EdSelRange (
  POS  *S,
  POS  *E
  )
{
  if (PosCmp (gEd.Cursor, gEd.Anchor) < 0) {
    *S = gEd.Cursor;
    *E = gEd.Anchor;
  } else {
    *S = gEd.Anchor;
    *E = gEd.Cursor;
  }
}

UINTN
EdVisCol (
  UINTN  Line,
  UINTN  Col
  )
{
  LINE   *L;
  UINTN  I, V;

  L = &gEd.Doc.Lines[Line];
  V = 0;
  for (I = 0; I < Col && I < L->Len; I++) {
    if (L->Text[I] == L'\t') {
      V = (V / TAB_SIZE + 1) * TAB_SIZE;
    } else {
      V++;
    }
  }

  return V;
}

UINTN
EdColFromVis (
  UINTN  Line,
  UINTN  VCol
  )
{
  LINE   *L;
  UINTN  I, V;

  L = &gEd.Doc.Lines[Line];
  V = 0;
  for (I = 0; I < L->Len; I++) {
    if (V >= VCol) {
      return I;
    }

    V = (L->Text[I] == L'\t') ? (V / TAB_SIZE + 1) * TAB_SIZE : V + 1;
  }

  return L->Len;
}

VOID
EdEnsureVisible (
  VOID
  )
{
  UINTN  V, Row;

  if (gEd.Rows == 0) {
    return;
  }

  Row = ViewRowOf (gEd.Cursor);
  if (Row < gEd.TopLine) {
    gEd.TopLine = Row;
  } else if (Row >= gEd.TopLine + gEd.Rows) {
    gEd.TopLine = Row - gEd.Rows + 1;
  }

  if (gEd.WordWrap) {
    gEd.LeftCol = 0;
    return;
  }

  V = EdVisCol (gEd.Cursor.Line, gEd.Cursor.Col);
  if (V < gEd.LeftCol) {
    gEd.LeftCol = (V > 8) ? V - 8 : 0;
  } else if ((gEd.Cols > 0) && (V >= gEd.LeftCol + gEd.Cols)) {
    gEd.LeftCol = V - gEd.Cols + 8;
  }
}

VOID
EdMoveCursor (
  POS      P,
  BOOLEAN  Extend
  )
{
  if (P.Line >= gEd.Doc.Count) {
    P = DocEndPos (&gEd.Doc);
  }

  if (P.Col > gEd.Doc.Lines[P.Line].Len) {
    P.Col = gEd.Doc.Lines[P.Line].Len;
  }

  gEd.Cursor   = P;
  gEd.Upstream = FALSE;
  if (!Extend) {
    gEd.Anchor = P;
  }

  gEd.PreferredVCol = ViewRowVCol (P);
  gEd.CanCoalesce   = FALSE;
  gEd.CaretOn       = TRUE;
  gEd.CaretTick     = gInput.Ticks;
  EdEnsureVisible ();
}

//
// Character classes for word movement: 0 space, 1 word, 2 punctuation.
//
STATIC
UINTN
CharClass (
  CHAR16  C
  )
{
  if ((C == L' ') || (C == L'\t')) {
    return 0;
  }

  if (((C >= L'a') && (C <= L'z')) || ((C >= L'A') && (C <= L'Z')) ||
      ((C >= L'0') && (C <= L'9')) || (C == L'_') || (C >= 0x80))
  {
    return 1;
  }

  return 2;
}

VOID
EdMoveLeft (
  BOOLEAN  Extend,
  BOOLEAN  Word
  )
{
  POS    P;
  LINE   *L;
  UINTN  Cls;

  P = gEd.Cursor;
  if (!Extend && EdHasSel () && !Word) {
    POS  S, E;

    EdSelRange (&S, &E);
    EdMoveCursor (S, FALSE);
    return;
  }

  if (P.Col == 0) {
    if (P.Line > 0) {
      P.Line--;
      P.Col = gEd.Doc.Lines[P.Line].Len;
    }
  } else if (!Word) {
    P.Col--;
  } else {
    L = &gEd.Doc.Lines[P.Line];
    while (P.Col > 0 && CharClass (L->Text[P.Col - 1]) == 0) {
      P.Col--;
    }

    if (P.Col > 0) {
      Cls = CharClass (L->Text[P.Col - 1]);
      while (P.Col > 0 && CharClass (L->Text[P.Col - 1]) == Cls) {
        P.Col--;
      }
    }
  }

  EdMoveCursor (P, Extend);
}

VOID
EdMoveRight (
  BOOLEAN  Extend,
  BOOLEAN  Word
  )
{
  POS    P;
  LINE   *L;
  UINTN  Cls;

  P = gEd.Cursor;
  L = &gEd.Doc.Lines[P.Line];
  if (!Extend && EdHasSel () && !Word) {
    POS  S, E;

    EdSelRange (&S, &E);
    EdMoveCursor (E, FALSE);
    return;
  }

  if (P.Col >= L->Len) {
    if (P.Line + 1 < gEd.Doc.Count) {
      P.Line++;
      P.Col = 0;
    }
  } else if (!Word) {
    P.Col++;
  } else {
    Cls = CharClass (L->Text[P.Col]);
    while (P.Col < L->Len && CharClass (L->Text[P.Col]) == Cls && Cls != 0) {
      P.Col++;
    }

    while (P.Col < L->Len && CharClass (L->Text[P.Col]) == 0) {
      P.Col++;
    }
  }

  EdMoveCursor (P, Extend);
}

VOID
EdMoveUpDown (
  INTN     Delta,
  BOOLEAN  Extend
  )
{
  INTN   Row, N;
  UINTN  Pref;

  Pref = gEd.PreferredVCol;
  N    = (INTN)ViewRowCount ();
  Row  = (INTN)ViewRowOf (gEd.Cursor) + Delta;
  if (Row < 0) {
    Row = 0;
  }

  if (Row >= N) {
    Row = N - 1;
  }

  EdMoveCursor (ViewPosFromRowVCol ((UINTN)Row, Pref), Extend);
  gEd.PreferredVCol = Pref;
}

VOID
EdHome (
  BOOLEAN  Extend,
  BOOLEAN  Doc
  )
{
  POS    P;
  LINE   *L;
  UINTN  Indent;

  if (Doc) {
    EdMoveCursor (MkPos (0, 0), Extend);
    return;
  }

  if (gEd.WordWrap) {
    UINTN  Ln, St, En;

    ViewRowGet (ViewRowOf (gEd.Cursor), &Ln, &St, &En);
    if (St > 0) {
      P     = gEd.Cursor;
      P.Col = St;
      EdMoveCursor (P, Extend);
      return;
    }
  }

  //
  // Smart home: first non-blank, then column 0.
  //
  L      = CurLine ();
  Indent = 0;
  while (Indent < L->Len && (L->Text[Indent] == L' ' || L->Text[Indent] == L'\t')) {
    Indent++;
  }

  P     = gEd.Cursor;
  P.Col = (P.Col == Indent) ? 0 : Indent;
  EdMoveCursor (P, Extend);
}

VOID
EdEnd (
  BOOLEAN  Extend,
  BOOLEAN  Doc
  )
{
  POS  P;

  if (Doc) {
    EdMoveCursor (DocEndPos (&gEd.Doc), Extend);
    return;
  }

  P     = gEd.Cursor;
  P.Col = CurLine ()->Len;
  if (gEd.WordWrap) {
    UINTN  Row, Ln, St, En;

    Row = ViewRowOf (gEd.Cursor);
    if (!ViewRowIsLast (Row)) {
      ViewRowGet (Row, &Ln, &St, &En);
      P.Col = En;
      EdMoveCursor (P, Extend);
      gEd.Upstream      = TRUE;
      gEd.PreferredVCol = ViewRowVCol (P);
      return;
    }
  }

  EdMoveCursor (P, Extend);
}

VOID
EdSelectAll (
  VOID
  )
{
  gEd.Anchor = MkPos (0, 0);
  gEd.Cursor = DocEndPos (&gEd.Doc);
  EdEnsureVisible ();
}

VOID
EdSelectWord (
  POS  P
  )
{
  LINE   *L;
  UINTN  S, E, Cls;

  L = &gEd.Doc.Lines[P.Line];
  if (L->Len == 0) {
    EdMoveCursor (P, FALSE);
    return;
  }

  S = MIN (P.Col, L->Len - 1);
  if ((P.Col == L->Len) && (S > 0)) {
    S = L->Len - 1;
  }

  Cls = CharClass (L->Text[S]);
  E   = S;
  while (S > 0 && CharClass (L->Text[S - 1]) == Cls) {
    S--;
  }

  while (E < L->Len && CharClass (L->Text[E]) == Cls) {
    E++;
  }

  gEd.Anchor = MkPos (P.Line, S);
  gEd.Cursor = MkPos (P.Line, E);
  gEd.PreferredVCol = ViewRowVCol (gEd.Cursor);
}

//
// ---------------- Editing ----------------
//
VOID
EdDeleteSel (
  VOID
  )
{
  POS  S, E;

  if (!EdHasSel ()) {
    return;
  }

  EdSelRange (&S, &E);
  RecDelete (S, E);
  gEd.Cursor = S;
  gEd.Anchor = S;
  EdEnsureVisible ();
}

VOID
EdInsertText (
  CONST CHAR16  *Str,
  UINTN         Len
  )
{
  POS  End;

  if (EdHasSel ()) {
    EdNewGroup ();
    EdDeleteSel ();
  }

  End          = RecInsert (gEd.Cursor, Str, Len);
  gEd.Cursor   = End;
  gEd.Anchor   = End;
  gEd.Upstream = (Len > 0) && (Str[Len - 1] != L'\n');
  gEd.PreferredVCol = ViewRowVCol (End);
  gEd.CaretOn       = TRUE;
  gEd.CaretTick     = gInput.Ticks;
  EdEnsureVisible ();
}

VOID
EdBackspace (
  VOID
  )
{
  POS  S;

  EdNewGroup ();
  if (EdHasSel ()) {
    EdDeleteSel ();
    return;
  }

  S = gEd.Cursor;
  if (S.Col > 0) {
    S.Col--;
  } else if (S.Line > 0) {
    S.Line--;
    S.Col = gEd.Doc.Lines[S.Line].Len;
  } else {
    return;
  }

  RecDelete (S, gEd.Cursor);
  EdMoveCursor (S, FALSE);
}

VOID
EdDeleteFwd (
  VOID
  )
{
  POS  E;

  EdNewGroup ();
  if (EdHasSel ()) {
    EdDeleteSel ();
    return;
  }

  E = gEd.Cursor;
  if (E.Col < CurLine ()->Len) {
    E.Col++;
  } else if (E.Line + 1 < gEd.Doc.Count) {
    E.Line++;
    E.Col = 0;
  } else {
    return;
  }

  RecDelete (gEd.Cursor, E);
  EdMoveCursor (gEd.Cursor, FALSE);
}

STATIC
VOID
ApplyRec (
  UNDO_REC  *R,
  BOOLEAN   Inverse
  )
{
  BOOLEAN  DoInsert;
  POS      End;

  DoInsert = (R->Type == UNDO_INSERT) ? !Inverse : Inverse;
  if (DoInsert) {
    DocInsert (&gEd.Doc, R->At, R->Text, R->Len, &End);
    gEd.Cursor = End;
  } else {
    DocDelete (&gEd.Doc, R->At, PosAdvance (R->At, R->Text, R->Len));
    gEd.Cursor = R->At;
  }
}

VOID
EdUndo (
  VOID
  )
{
  UINTN     G;
  UNDO_REC  R;
  POS       Before;

  if (gEd.Undo.Count == 0) {
    return;
  }

  G      = gEd.Undo.Items[gEd.Undo.Count - 1].Group;
  Before = gEd.Cursor;
  while (gEd.Undo.Count > 0 && gEd.Undo.Items[gEd.Undo.Count - 1].Group == G) {
    R = gEd.Undo.Items[--gEd.Undo.Count];
    ApplyRec (&R, TRUE);
    Before = R.CursorBefore;
    StackPush (&gEd.Redo, &R);
  }

  Touched ();
  EdMoveCursor (Before, FALSE);
  EdNewGroup ();
}

VOID
EdRedo (
  VOID
  )
{
  UINTN     G;
  UNDO_REC  R;

  if (gEd.Redo.Count == 0) {
    return;
  }

  G = gEd.Redo.Items[gEd.Redo.Count - 1].Group;
  while (gEd.Redo.Count > 0 && gEd.Redo.Items[gEd.Redo.Count - 1].Group == G) {
    R = gEd.Redo.Items[--gEd.Redo.Count];
    ApplyRec (&R, FALSE);
    StackPush (&gEd.Undo, &R);
  }

  Touched ();
  EdMoveCursor (gEd.Cursor, FALSE);
  EdNewGroup ();
}

VOID
EdCopy (
  VOID
  )
{
  POS  S, E;

  if (!EdHasSel ()) {
    return;
  }

  EdSelRange (&S, &E);
  if (gEd.Clip != NULL) {
    FreePool (gEd.Clip);
  }

  gEd.Clip = DocGetText (&gEd.Doc, S, E, &gEd.ClipLen);
}

VOID
EdCut (
  VOID
  )
{
  if (!EdHasSel ()) {
    return;
  }

  EdCopy ();
  EdNewGroup ();
  EdDeleteSel ();
  EdNewGroup ();
}

VOID
EdPaste (
  VOID
  )
{
  if ((gEd.Clip == NULL) || (gEd.ClipLen == 0)) {
    return;
  }

  EdNewGroup ();
  EdInsertText (gEd.Clip, gEd.ClipLen);
  EdNewGroup ();
}

VOID
EdInsertTimeDate (
  VOID
  )
{
  EFI_TIME  T;
  CHAR16    Buf[32];
  UINTN     Hour;

  if (EFI_ERROR (gRT->GetTime (&T, NULL))) {
    return;
  }

  Hour = T.Hour % 12;
  if (Hour == 0) {
    Hour = 12;
  }

  UnicodeSPrint (
    Buf,
    sizeof (Buf),
    L"%d:%02d %s %d/%d/%04d",
    Hour,
    T.Minute,
    (T.Hour < 12) ? L"AM" : L"PM",
    T.Month,
    T.Day,
    T.Year
    );
  EdNewGroup ();
  EdInsertText (Buf, StrLen (Buf));
  EdNewGroup ();
}

VOID
EdGotoLine (
  UINTN  Line
  )
{
  if (Line == 0) {
    Line = 1;
  }

  if (Line > gEd.Doc.Count) {
    Line = gEd.Doc.Count;
  }

  EdMoveCursor (MkPos (Line - 1, 0), FALSE);
  if (gEd.Rows > 0) {
    UINTN  Row;

    Row = ViewRowOf (gEd.Cursor);
    ViewScrollTo ((Row > gEd.Rows / 2) ? (INTN)(Row - gEd.Rows / 2) : 0);
  }
}

//
// ---------------- Find / Replace ----------------
//
STATIC
CHAR16
Fold (
  CHAR16  C
  )
{
  if (!gEd.MatchCase && (C >= L'A') && (C <= L'Z')) {
    return (CHAR16)(C - L'A' + L'a');
  }

  return C;
}

/**
  Search one line for Needle starting at From. Returns column or -1.
**/
STATIC
INTN
FindInLine (
  LINE          *L,
  UINTN         From,
  CONST CHAR16  *Needle,
  UINTN         NLen
  )
{
  UINTN  I, J;

  if ((NLen == 0) || (L->Len < NLen)) {
    return -1;
  }

  for (I = From; I + NLen <= L->Len; I++) {
    for (J = 0; J < NLen; J++) {
      if (Fold (L->Text[I + J]) != Fold (Needle[J])) {
        break;
      }
    }

    if (J == NLen) {
      return (INTN)I;
    }
  }

  return -1;
}

BOOLEAN
EdFindNext (
  CONST CHAR16  *Needle,
  BOOLEAN       Wrap
  )
{
  UINTN  NLen, Line, From, Pass, Scanned;
  INTN   Col;
  POS    S, E;

  NLen = StrLen (Needle);
  if (NLen == 0) {
    return FALSE;
  }

  EdSelRange (&S, &E);
  Line    = E.Line;
  From    = E.Col;
  Scanned = 0;
  for (Pass = 0; Scanned <= gEd.Doc.Count; Pass++) {
    Col = FindInLine (&gEd.Doc.Lines[Line], From, Needle, NLen);
    if (Col >= 0) {
      gEd.Anchor = MkPos (Line, (UINTN)Col);
      gEd.Cursor = MkPos (Line, (UINTN)Col + NLen);
      gEd.PreferredVCol = ViewRowVCol (gEd.Cursor);
      EdEnsureVisible ();
      return TRUE;
    }

    From = 0;
    Line++;
    Scanned++;
    if (Line >= gEd.Doc.Count) {
      if (!Wrap) {
        return FALSE;
      }

      Line = 0;
    }
  }

  return FALSE;
}

UINTN
EdReplaceAll (
  CONST CHAR16  *Needle,
  CONST CHAR16  *Repl
  )
{
  UINTN  NLen, RLen, Line, From, Count;
  INTN   Col;

  NLen  = StrLen (Needle);
  RLen  = StrLen (Repl);
  Count = 0;
  if (NLen == 0) {
    return 0;
  }

  EdNewGroup ();
  for (Line = 0; Line < gEd.Doc.Count; Line++) {
    From = 0;
    for ( ; ;) {
      Col = FindInLine (&gEd.Doc.Lines[Line], From, Needle, NLen);
      if (Col < 0) {
        break;
      }

      RecDelete (MkPos (Line, (UINTN)Col), MkPos (Line, (UINTN)Col + NLen));
      if (RLen > 0) {
        RecInsert (MkPos (Line, (UINTN)Col), Repl, RLen);
      }

      From = (UINTN)Col + RLen;
      Count++;
    }
  }

  EdNewGroup ();
  if (Count > 0) {
    EdMoveCursor (gEd.Cursor, FALSE);
  }

  return Count;
}
