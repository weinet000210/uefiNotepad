/** @file
  uefiNotepad entry point, event loop, commands and input dispatch.

  Usage:  uefinotepad [file]

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

#define BLINK_TICKS   26     // ~520 ms at 20 ms per tick
#define WHEEL_LINES   3

UINTN         gModal;
STATIC UINTN  mLastBlink;
STATIC UINTN  mHoverKey;     // cheap fingerprint of hover state
STATIC BOOLEAN mMenuPressed; // mouse went down on a menu title this click
STATIC BOOLEAN mClosePressed;

VOID
FullRedraw (
  VOID
  )
{
  ViewRender ();
  GfxPresent ();
}

VOID
UpdateMouseCursor (
  VOID
  )
{
  BOOLEAN  IBeam;

  if (!gInput.HasMouse) {
    return;
  }

  IBeam = (gModal == 0) && (gEd.OpenMenu < 0) && (ViewHitTest (gInput.MouseX, gInput.MouseY) == HIT_TEXT);
  GfxDrawMouse (gInput.MouseX, gInput.MouseY, IBeam);
}

//
// ---------------- File commands ----------------
//
STATIC
VOID
ShowError (
  CONST CHAR16  *What,
  CONST CHAR16  *Path,
  EFI_STATUS    Status
  )
{
  CHAR16  Buf[MAX_PATH + 96];

  UnicodeSPrint (Buf, sizeof (Buf), L"%s\n%s\n\n%r", What, Path, Status);
  DlgMessage (NP_APPNAME, Buf, L"OK", NULL, NULL);
}

STATIC
BOOLEAN
DoSaveAs (
  VOID
  )
{
  CHAR16      Path[MAX_PATH];
  EFI_STATUS  Status;
  CHAR16      Msg[MAX_PATH + 64];
  UINTN       Enc;

  StrCpyS (Path, MAX_PATH, gEd.Path);
  Enc = gEd.Enc;
  if (!FileDialog (TRUE, Path, MAX_PATH, &Enc) || (Path[0] == 0)) {
    return FALSE;
  }

  if ((StrCmp (Path, gEd.Path) != 0) && FileExists (Path)) {
    UnicodeSPrint (Msg, sizeof (Msg), L"%s already exists.\nDo you want to replace it?", Path);
    if (DlgMessage (L"Confirm Save As", Msg, L"Yes", L"No", NULL) != 1) {
      return FALSE;
    }
  }

  Status = FileSave (Path, &gEd.Doc, Enc, gEd.Eol);
  if (EFI_ERROR (Status)) {
    ShowError (L"Cannot save file:", Path, Status);
    return FALSE;
  }

  gEd.Enc = Enc;
  StrCpyS (gEd.Path, MAX_PATH, Path);
  gEd.Modified = FALSE;
  EdSetMessage (L"Saved.");
  return TRUE;
}

STATIC
BOOLEAN
DoSave (
  VOID
  )
{
  EFI_STATUS  Status;

  if (gEd.Path[0] == 0) {
    return DoSaveAs ();
  }

  Status = FileSave (gEd.Path, &gEd.Doc, gEd.Enc, gEd.Eol);
  if (EFI_ERROR (Status)) {
    ShowError (L"Cannot save file:", gEd.Path, Status);
    return FALSE;
  }

  gEd.Modified = FALSE;
  EdSetMessage (L"Saved.");
  return TRUE;
}

/**
  Ask to save unsaved changes. Returns FALSE if the user cancelled.
**/
STATIC
BOOLEAN
ConfirmDiscard (
  VOID
  )
{
  CHAR16  Msg[MAX_PATH + 64];
  INTN    R;

  if (!gEd.Modified) {
    return TRUE;
  }

  UnicodeSPrint (
    Msg,
    sizeof (Msg),
    L"Do you want to save changes to %s?",
    (gEd.Path[0] != 0) ? gEd.Path : L"Untitled"
    );
  R = DlgMessage (NP_APPNAME, Msg, L"Save", L"Don't Save", L"Cancel");
  if (R == 1) {
    return DoSave ();
  }

  return R == 2;
}

STATIC
VOID
ResetView (
  VOID
  )
{
  POS  Zero;

  Zero.Line   = 0;
  Zero.Col    = 0;
  gEd.TopLine = 0;
  gEd.LeftCol = 0;
  gEd.Modified     = FALSE;
  gEd.MaxColsDirty = TRUE;
  gEd.WrapDirty    = TRUE;
  EdClearUndo ();
  EdMoveCursor (Zero, FALSE);
}

STATIC
VOID
DoNew (
  VOID
  )
{
  if (!ConfirmDiscard ()) {
    return;
  }

  DocFree (&gEd.Doc);
  DocInit (&gEd.Doc);
  gEd.Path[0] = 0;
  gEd.Enc     = ENC_UTF8;
  gEd.Eol     = EOL_CRLF;
  ResetView ();
}

STATIC
VOID
OpenPath (
  CONST CHAR16  *Path,
  BOOLEAN       Quiet
  )
{
  EFI_STATUS  Status;
  UINTN       Enc, Eol;

  Status = FileLoad (Path, &gEd.Doc, &Enc, &Eol);
  if (EFI_ERROR (Status)) {
    if (!Quiet) {
      ShowError (L"Cannot open file:", Path, Status);
    }

    return;
  }

  StrCpyS (gEd.Path, MAX_PATH, Path);
  gEd.Enc = Enc;
  gEd.Eol = Eol;
  ResetView ();
}

STATIC
VOID
DoOpen (
  VOID
  )
{
  CHAR16  Path[MAX_PATH];
  UINTN   Enc;

  if (!ConfirmDiscard ()) {
    return;
  }

  StrCpyS (Path, MAX_PATH, gEd.Path);
  Enc = gEd.Enc;
  if (!FileDialog (FALSE, Path, MAX_PATH, &Enc) || (Path[0] == 0)) {
    return;
  }

  OpenPath (Path, FALSE);
}

STATIC
VOID
DoFind (
  BOOLEAN  Ask
  )
{
  CHAR16  Msg[MAX_FIELD + 32];

  if (Ask || (gEd.FindText[0] == 0)) {
    if (EdHasSel ()) {
      POS     S, E;
      CHAR16  *T;
      UINTN   Len;

      EdSelRange (&S, &E);
      if ((S.Line == E.Line) && (E.Col - S.Col < MAX_FIELD)) {
        T = DocGetText (&gEd.Doc, S, E, &Len);
        if (T != NULL) {
          StrCpyS (gEd.FindText, MAX_FIELD, T);
          FreePool (T);
        }
      }
    }

    if (!DlgInput (L"Find", L"Find what:", gEd.FindText, MAX_FIELD) || (gEd.FindText[0] == 0)) {
      return;
    }
  }

  if (!EdFindNext (gEd.FindText, TRUE)) {
    UnicodeSPrint (Msg, sizeof (Msg), L"Cannot find \"%s\"", gEd.FindText);
    DlgMessage (NP_APPNAME, Msg, L"OK", NULL, NULL);
  }
}

STATIC
BOOLEAN
SameNoCase (
  CONST CHAR16  *A,
  CONST CHAR16  *B
  )
{
  CHAR16  Ca, Cb;

  for ( ; ; A++, B++) {
    Ca = ((*A >= L'A') && (*A <= L'Z')) ? (CHAR16)(*A + 32) : *A;
    Cb = ((*B >= L'A') && (*B <= L'Z')) ? (CHAR16)(*B + 32) : *B;
    if (Ca != Cb) {
      return FALSE;
    }

    if (Ca == 0) {
      return TRUE;
    }
  }
}

STATIC
VOID
DoReplace (
  VOID
  )
{
  INTN    R;
  UINTN   N;
  CHAR16  Msg[64];
  POS     S, E;
  CHAR16  *Sel;
  UINTN   Len;

  R = DlgReplace (gEd.FindText, gEd.ReplText, MAX_FIELD);
  if ((R == 0) || (gEd.FindText[0] == 0)) {
    return;
  }

  if (R == 2) {
    N = EdReplaceAll (gEd.FindText, gEd.ReplText);
    UnicodeSPrint (Msg, sizeof (Msg), L"Replaced %d occurrence(s).", N);
    EdSetMessage (Msg);
    return;
  }

  //
  // Replace one: if the selection is the match, replace it, then find next.
  //
  if (EdHasSel ()) {
    EdSelRange (&S, &E);
    Sel = DocGetText (&gEd.Doc, S, E, &Len);
    if (Sel != NULL) {
      if (gEd.MatchCase ? (StrCmp (Sel, gEd.FindText) == 0) : SameNoCase (Sel, gEd.FindText)) {
        EdNewGroup ();
        EdInsertText (gEd.ReplText, StrLen (gEd.ReplText));
        EdNewGroup ();
      }

      FreePool (Sel);
    }
  }

  if (!EdFindNext (gEd.FindText, TRUE)) {
    EdSetMessage (L"No more matches.");
  }
}

STATIC
VOID
DoGoto (
  VOID
  )
{
  CHAR16  Buf[16];

  UnicodeSPrint (Buf, sizeof (Buf), L"%d", gEd.Cursor.Line + 1);
  if (DlgInput (L"Go To Line", L"Line number:", Buf, ARRAY_SIZE (Buf)) && (Buf[0] != 0)) {
    EdGotoLine (StrDecimalToUintn (Buf));
  }
}

STATIC CONST CHAR16  mKeysText[] =
  L"File     Ctrl+N New   Ctrl+O Open   Ctrl+S Save   Ctrl+Q Exit\n"
  L"Edit     Ctrl+Z Undo  Ctrl+Y Redo   Ctrl+X/C/V Cut/Copy/Paste\n"
  L"         Ctrl+A Select all          F5 Insert time/date\n"
  L"Search   Ctrl+F Find  F3 Find next  Ctrl+H Replace  Ctrl+G Go to\n"
  L"Move     Arrows, Home/End, PgUp/PgDn, Ctrl+Arrows by word\n"
  L"         Ctrl+Home/End start/end of file, Shift extends selection\n"
  L"Menus    F10 or Alt+letter, arrows to move, Enter to choose\n"
  L"Mouse    Click, drag to select, double-click a word, wheel scrolls";

VOID
RunCommand (
  UINTN  Cmd
  )
{
  gEd.OpenMenu     = -1;
  gEd.MenuHot      = -1;
  gEd.SubOpen      = -1;
  gEd.SubHot       = -1;
  gEd.MenuKeyboard = FALSE;
  FullRedraw ();

  if ((Cmd >= CMD_ENC_FIRST) && (Cmd <= CMD_ENC_LAST)) {
    if (gEd.Enc != Cmd - CMD_ENC_FIRST) {
      gEd.Enc      = Cmd - CMD_ENC_FIRST;
      gEd.Modified = TRUE;
    }

    return;
  }

  switch (Cmd) {
    case CMD_NEW:       DoNew ();
      break;
    case CMD_OPEN:      DoOpen ();
      break;
    case CMD_SAVE:      DoSave ();
      break;
    case CMD_SAVEAS:    DoSaveAs ();
      break;
    case CMD_EXIT:
      if (ConfirmDiscard ()) {
        gEd.Quit = TRUE;
      }

      break;
    case CMD_UNDO:      EdUndo ();
      break;
    case CMD_REDO:      EdRedo ();
      break;
    case CMD_CUT:       EdCut ();
      break;
    case CMD_COPY:      EdCopy ();
      break;
    case CMD_PASTE:     EdPaste ();
      break;
    case CMD_DELETE:    EdDeleteFwd ();
      break;
    case CMD_SELECTALL: EdSelectAll ();
      break;
    case CMD_TIMEDATE:  EdInsertTimeDate ();
      break;
    case CMD_FIND:      DoFind (TRUE);
      break;
    case CMD_FINDNEXT:  DoFind (FALSE);
      break;
    case CMD_REPLACE:   DoReplace ();
      break;
    case CMD_GOTO:      DoGoto ();
      break;
    case CMD_EOL_CRLF:
    case CMD_EOL_LF:
      if (gEd.Eol != ((Cmd == CMD_EOL_CRLF) ? EOL_CRLF : EOL_LF)) {
        gEd.Eol      = (Cmd == CMD_EOL_CRLF) ? EOL_CRLF : EOL_LF;
        gEd.Modified = TRUE;
      }

      break;
    case CMD_WORDWRAP:
      gEd.WordWrap  = !gEd.WordWrap;
      gEd.WrapDirty = TRUE;
      gEd.LeftCol   = 0;
      ViewLayout ();
      EdEnsureVisible ();
      ViewScrollTo ((INTN)gEd.TopLine);
      break;
    case CMD_STATUSBAR:
      gEd.ShowStatus = !gEd.ShowStatus;
      break;
    case CMD_KEYS:
      DlgMessage (L"Keyboard Shortcuts", mKeysText, L"OK", NULL, NULL);
      break;
    case CMD_ABOUT:
      DlgMessage (
        L"About uefiNotepad",
        L"uefiNotepad " NP_VERSION L"\n"
        L"A Notepad-style text editor for the UEFI Shell.\n\n"
        L"github.com/weinet000210/uefiNotepad",
        L"OK",
        NULL,
        NULL
        );
      break;
    default:
      break;
  }

  ViewLayout ();
  EdEnsureVisible ();
}

//
// ---------------- Menus ----------------
//
STATIC
INTN
FirstEnabled (
  UINTN  Menu,
  INTN   From,
  INTN   Dir
  )
{
  CONST MENU  *M;
  INTN        I, N, Tries;

  M = &gMenus[Menu];
  N = (INTN)M->Count;
  I = From;
  for (Tries = 0; Tries < N; Tries++) {
    I = (I + Dir + N) % N;
    if (M->Items[I].Label != NULL) {
      return I;
    }
  }

  return -1;
}

STATIC
VOID
OpenMenu (
  INTN     Index,
  BOOLEAN  Keyboard
  )
{
  gEd.OpenMenu     = Index;
  gEd.SubOpen      = -1;
  gEd.SubHot       = -1;
  gEd.MenuKeyboard = Keyboard;
  gEd.MenuHot      = Keyboard ? FirstEnabled ((UINTN)Index, -1, 1) : -1;
}

STATIC
VOID
CloseMenu (
  VOID
  )
{
  gEd.OpenMenu     = -1;
  gEd.MenuHot      = -1;
  gEd.SubOpen      = -1;
  gEd.SubHot       = -1;
  gEd.MenuKeyboard = FALSE;
}

STATIC
VOID
MenuKey (
  INPUT_EVENT  *Ev
  )
{
  CONST MENU  *M;
  UINTN       I;

  M = &gMenus[gEd.OpenMenu];

  //
  // Submenu open: it takes the arrows and Enter.
  //
  if (gEd.SubOpen >= 0) {
    CONST MENU_ITEM  *Par;

    Par = &M->Items[gEd.SubOpen];
    if ((Ev->Scan == SCAN_ESC) || (Ev->Scan == SCAN_LEFT)) {
      gEd.SubOpen = -1;
      gEd.SubHot  = -1;
      return;
    }

    if (Ev->Scan == SCAN_UP) {
      gEd.SubHot = (gEd.SubHot <= 0) ? (INTN)Par->SubCount - 1 : gEd.SubHot - 1;
      return;
    }

    if (Ev->Scan == SCAN_DOWN) {
      gEd.SubHot = (gEd.SubHot + 1) % (INTN)Par->SubCount;
      return;
    }

    if (((Ev->Char == CHAR_CARRIAGE_RETURN) || (Ev->Char == CHAR_LINEFEED) || (Ev->Char == L' ')) &&
        (gEd.SubHot >= 0))
    {
      RunCommand (Par->Sub[gEd.SubHot].Cmd);
      return;
    }

    if (Ev->Scan != SCAN_RIGHT) {
      return;
    }
  } else if ((gEd.MenuHot >= 0) && (M->Items[gEd.MenuHot].Sub != NULL) &&
             ((Ev->Scan == SCAN_RIGHT) || (Ev->Char == CHAR_CARRIAGE_RETURN) ||
              (Ev->Char == CHAR_LINEFEED) || (Ev->Char == L' ')))
  {
    gEd.SubOpen = gEd.MenuHot;
    gEd.SubHot  = 0;
    return;
  }

  switch (Ev->Scan) {
    case SCAN_ESC:
    case SCAN_F10:
      CloseMenu ();
      return;
    case SCAN_LEFT:
      OpenMenu ((gEd.OpenMenu + (INTN)gMenuCount - 1) % (INTN)gMenuCount, TRUE);
      return;
    case SCAN_RIGHT:
      OpenMenu ((gEd.OpenMenu + 1) % (INTN)gMenuCount, TRUE);
      return;
    case SCAN_UP:
      gEd.MenuHot = FirstEnabled ((UINTN)gEd.OpenMenu, (gEd.MenuHot < 0) ? 0 : gEd.MenuHot, -1);
      return;
    case SCAN_DOWN:
      gEd.MenuHot = FirstEnabled ((UINTN)gEd.OpenMenu, gEd.MenuHot, 1);
      return;
    default:
      break;
  }

  if ((Ev->Char == CHAR_CARRIAGE_RETURN) || (Ev->Char == CHAR_LINEFEED) || (Ev->Char == L' ')) {
    if ((gEd.MenuHot >= 0) && MenuItemEnabled (&M->Items[gEd.MenuHot])) {
      RunCommand (M->Items[gEd.MenuHot].Cmd);
    }

    return;
  }

  //
  // Alt+letter while open switches menus; a plain letter picks the item
  // whose label starts with it.
  //
  if (Ev->Char != 0) {
    CHAR16  C;

    C = Ev->Char | 0x20;
    if (Ev->Alt) {
      for (I = 0; I < gMenuCount; I++) {
        if (gMenus[I].Hotkey == C) {
          OpenMenu ((INTN)I, TRUE);
          return;
        }
      }
    }

    for (I = 0; I < M->Count; I++) {
      if ((M->Items[I].Label != NULL) && ((M->Items[I].Label[0] | 0x20) == C)) {
        if (M->Items[I].Sub != NULL) {
          gEd.MenuHot = (INTN)I;
          gEd.SubOpen = (INTN)I;
          gEd.SubHot  = 0;
        } else {
          RunCommand (M->Items[I].Cmd);
        }

        return;
      }
    }
  }
}

//
// ---------------- Keyboard ----------------
//
STATIC
VOID
EditorKey (
  INPUT_EVENT  *Ev
  )
{
  BOOLEAN  Sh;
  UINTN    I;

  Sh          = Ev->Shift;
  gEd.Message[0] = 0;

  if (Ev->Alt && (Ev->Char != 0)) {
    for (I = 0; I < gMenuCount; I++) {
      if (gMenus[I].Hotkey == (Ev->Char | 0x20)) {
        OpenMenu ((INTN)I, TRUE);
        return;
      }
    }
  }

  switch (Ev->Scan) {
    case SCAN_UP:        if (Ev->Ctrl) {
                           ViewScrollTo ((INTN)gEd.TopLine - 1);
                         } else {
                           EdMoveUpDown (-1, Sh);
                         }

      return;
    case SCAN_DOWN:      if (Ev->Ctrl) {
                           ViewScrollTo ((INTN)gEd.TopLine + 1);
                         } else {
                           EdMoveUpDown (1, Sh);
                         }

      return;
    case SCAN_LEFT:      EdMoveLeft (Sh, Ev->Ctrl);
      return;
    case SCAN_RIGHT:     EdMoveRight (Sh, Ev->Ctrl);
      return;
    case SCAN_HOME:      EdHome (Sh, Ev->Ctrl);
      return;
    case SCAN_END:       EdEnd (Sh, Ev->Ctrl);
      return;
    case SCAN_PAGE_UP:
      ViewScrollTo ((INTN)gEd.TopLine - (INTN)gEd.Rows);
      EdMoveUpDown (-(INTN)gEd.Rows, Sh);
      return;
    case SCAN_PAGE_DOWN:
      ViewScrollTo ((INTN)gEd.TopLine + (INTN)gEd.Rows);
      EdMoveUpDown ((INTN)gEd.Rows, Sh);
      return;
    case SCAN_DELETE:
      if (Sh) {
        EdCut ();
      } else {
        EdDeleteFwd ();
      }

      return;
    case SCAN_INSERT:
      if (Ev->Ctrl) {
        EdCopy ();
      } else if (Sh) {
        EdPaste ();
      }

      return;
    case SCAN_F1:        RunCommand (CMD_KEYS);
      return;
    case SCAN_F3:        RunCommand (CMD_FINDNEXT);
      return;
    case SCAN_F5:        EdInsertTimeDate ();
      return;
    case SCAN_F10:       OpenMenu (0, TRUE);
      return;
    case SCAN_F12:       RunCommand (CMD_SAVEAS);   // Ctrl+Shift+S fallback for consoles that drop Shift
      return;
    case SCAN_ESC:
      EdMoveCursor (gEd.Cursor, FALSE);
      return;
    default:
      break;
  }

  if (Ev->Ctrl) {
    switch (Ev->Char) {
      case L'n': RunCommand (CMD_NEW);
        return;
      case L'o': RunCommand (CMD_OPEN);
        return;
      case L's': RunCommand (Sh ? CMD_SAVEAS : CMD_SAVE);
        return;
      case L'q':
      case L'w': RunCommand (CMD_EXIT);
        return;
      case L'z': EdUndo ();
        return;
      case L'y': EdRedo ();
        return;
      case L'x': EdCut ();
        return;
      case L'c': EdCopy ();
        return;
      case L'v': EdPaste ();
        return;
      case L'a': EdSelectAll ();
        return;
      case L'f': RunCommand (CMD_FIND);
        return;
      case L'h': RunCommand (CMD_REPLACE);
        return;
      case L'g': RunCommand (CMD_GOTO);
        return;
      default:
        return;
    }
  }

  if (Ev->Char == CHAR_BACKSPACE) {
    EdBackspace ();
    return;
  }

  if ((Ev->Char == CHAR_CARRIAGE_RETURN) || (Ev->Char == CHAR_LINEFEED)) {
    EdNewGroup ();
    EdInsertText (L"\n", 1);
    EdNewGroup ();
    return;
  }

  if ((Ev->Char == CHAR_TAB) || (Ev->Char >= 0x20)) {
    CHAR16  C;

    C = Ev->Char;
    if (!gEd.CanCoalesce) {
      EdNewGroup ();
    }

    EdInsertText (&C, 1);
    gEd.CanCoalesce = (C != L' ') && (C != CHAR_TAB);
  }
}

//
// ---------------- Mouse ----------------
//
STATIC
VOID
ScrollBarPress (
  UINTN  Hit,
  INTN   X,
  INTN   Y
  )
{
  INTN  Ty, Th, Hy, Hh;

  if (Hit == HIT_VSCROLL) {
    ViewVScrollGeom (&Ty, &Th, &Hy, &Hh);
    if (Y < Ty) {
      ViewScrollTo ((INTN)gEd.TopLine - 1);
    } else if (Y >= Ty + Th) {
      ViewScrollTo ((INTN)gEd.TopLine + 1);
    } else if (Y < Hy) {
      ViewScrollTo ((INTN)gEd.TopLine - (INTN)gEd.Rows);
    } else if (Y >= Hy + Hh) {
      ViewScrollTo ((INTN)gEd.TopLine + (INTN)gEd.Rows);
    } else {
      gEd.Drag       = DRAG_VTHUMB;
      gEd.DragOffset = Y - Hy;
    }
  } else {
    ViewHScrollGeom (&Ty, &Th, &Hy, &Hh);
    if (X < Ty) {
      ViewScrollLeftTo ((INTN)gEd.LeftCol - 4);
    } else if (X >= Ty + Th) {
      ViewScrollLeftTo ((INTN)gEd.LeftCol + 4);
    } else if (X < Hy) {
      ViewScrollLeftTo ((INTN)gEd.LeftCol - (INTN)gEd.Cols);
    } else if (X >= Hy + Hh) {
      ViewScrollLeftTo ((INTN)gEd.LeftCol + (INTN)gEd.Cols);
    } else {
      gEd.Drag       = DRAG_HTHUMB;
      gEd.DragOffset = X - Hy;
    }
  }
}

STATIC
VOID
ThumbDrag (
  INTN  X,
  INTN  Y
  )
{
  INTN   Ty, Th, Hy, Hh;
  INTN   Range;
  UINTN  Max;

  if (gEd.Drag == DRAG_VTHUMB) {
    ViewVScrollGeom (&Ty, &Th, &Hy, &Hh);
    Range = Th - Hh;
    Max   = ViewMaxTop ();
    if (Range > 0) {
      ViewScrollTo ((Y - gEd.DragOffset - Ty) * (INTN)Max / Range);
    }
  } else if (gEd.Drag == DRAG_HTHUMB) {
    ViewHScrollGeom (&Ty, &Th, &Hy, &Hh);
    Range = Th - Hh;
    Max   = ViewMaxCols () + 1;
    Max   = (Max > gEd.Cols) ? Max - gEd.Cols : 0;
    if (Range > 0) {
      ViewScrollLeftTo ((X - gEd.DragOffset - Ty) * (INTN)Max / Range);
    }
  }
}

STATIC
VOID
TextDrag (
  INTN  X,
  INTN  Y
  )
{
  POS  P;

  //
  // Auto-scroll when dragging past the text area.
  //
  if (Y < gEd.TextY) {
    ViewScrollTo ((INTN)gEd.TopLine - 1);
  } else if (Y >= gEd.TextY + gEd.TextH) {
    ViewScrollTo ((INTN)gEd.TopLine + 1);
  }

  if (X < gEd.TextX) {
    ViewScrollLeftTo ((INTN)gEd.LeftCol - 2);
  } else if (X >= gEd.TextX + gEd.TextW) {
    ViewScrollLeftTo ((INTN)gEd.LeftCol + 2);
  }

  ViewMouseToPos (X, MIN (MAX (Y, gEd.TextY), gEd.TextY + gEd.TextH - 1), &P);
  gEd.Cursor        = P;
  gEd.PreferredVCol = ViewRowVCol (P);
  gEd.CaretOn       = TRUE;
}

STATIC
INTN
MenuTitleAt (
  INTN  X,
  INTN  Y
  )
{
  UINTN  I;
  INTN   Tx;

  if ((Y < TITLE_H) || (Y >= TITLE_H + MENU_H)) {
    return -1;
  }

  for (I = 0; I < gMenuCount; I++) {
    Tx = MenuTitleX (I);
    if ((X >= Tx) && (X < Tx + MenuTitleW (I))) {
      return (INTN)I;
    }
  }

  return -1;
}

STATIC
VOID
HandleMouse (
  INPUT_EVENT  *Ev
  )
{
  UINTN  Hit;
  INTN   Title, Item;
  POS    P;

  Hit = ViewHitTest (Ev->X, Ev->Y);

  //
  // Wheel.
  //
  if ((Ev->Wheel != 0) && (gEd.OpenMenu < 0)) {
    if (Ev->Shift) {
      ViewScrollLeftTo ((INTN)gEd.LeftCol + Ev->Wheel * 8);
    } else {
      ViewScrollTo ((INTN)gEd.TopLine + Ev->Wheel * WHEEL_LINES);
    }
  }

  //
  // Open popup menu: hover, click, release.
  //
  if (gEd.OpenMenu >= 0) {
    INTN  Sub;

    Title = MenuTitleAt (Ev->X, Ev->Y);
    Sub   = SubItemAt (Ev->X, Ev->Y);
    Item  = MenuItemAt ((UINTN)gEd.OpenMenu, Ev->X, Ev->Y);

    if (Sub >= -1) {
      //
      // Inside the submenu.
      //
      gEd.SubHot = Sub;
      if ((Ev->Released & MB_LEFT) && (Sub >= 0)) {
        mMenuPressed = FALSE;
        RunCommand (gMenus[gEd.OpenMenu].Items[gEd.SubOpen].Sub[Sub].Cmd);
      }

      return;
    }

    if (Item >= -1) {
      gEd.MenuHot = Item;
      if (Item >= 0) {
        if (gMenus[gEd.OpenMenu].Items[Item].Sub != NULL) {
          if (gEd.SubOpen != Item) {
            gEd.SubOpen = Item;
            gEd.SubHot  = -1;
          }
        } else {
          gEd.SubOpen = -1;
          gEd.SubHot  = -1;
        }
      }
    } else if (!gEd.MenuKeyboard && (gEd.SubOpen < 0)) {
      gEd.MenuHot = -1;
    }

    if ((Title >= 0) && (Title != gEd.OpenMenu)) {
      OpenMenu (Title, FALSE);
      return;
    }

    if (Ev->Pressed & MB_LEFT) {
      if (Title == gEd.OpenMenu) {
        if (!mMenuPressed) {
          CloseMenu ();
        }

        mMenuPressed = FALSE;
        return;
      }

      if (Item == -2) {
        CloseMenu ();
        return;
      }
    }

    if ((Ev->Released & MB_LEFT) && (Item >= 0) &&
        (gMenus[gEd.OpenMenu].Items[Item].Sub == NULL))
    {
      mMenuPressed = FALSE;
      RunCommand (gMenus[gEd.OpenMenu].Items[Item].Cmd);
      return;
    }

    if (Ev->Released & MB_LEFT) {
      mMenuPressed = FALSE;
    }

    return;
  }

  if ((Ev->Released & MB_LEFT) && mClosePressed) {
    mClosePressed = FALSE;
    if (Hit == HIT_CLOSE) {
      RunCommand (CMD_EXIT);
      return;
    }
  }

  if (Ev->Pressed & MB_LEFT) {
    gEd.Message[0] = 0;
    switch (Hit) {
      case HIT_CLOSE:
        mClosePressed = TRUE;
        return;
      case HIT_MENUBAR:
        Title = MenuTitleAt (Ev->X, Ev->Y);
        if (Title >= 0) {
          OpenMenu (Title, FALSE);
          mMenuPressed = TRUE;
        }

        return;
      case HIT_TEXT:
        ViewMouseToPos (Ev->X, Ev->Y, &P);
        if (Ev->DoubleClick) {
          EdSelectWord (P);
          gEd.Drag = DRAG_NONE;
        } else {
          EdMoveCursor (P, Ev->Shift);
          gEd.Drag = DRAG_TEXT;
        }

        return;
      case HIT_VSCROLL:
      case HIT_HSCROLL:
        ScrollBarPress (Hit, Ev->X, Ev->Y);
        return;
      default:
        return;
    }
  }

  if (Ev->Released & MB_LEFT) {
    gEd.Drag = DRAG_NONE;
    EdEnsureVisible ();
    return;
  }

  if (Ev->Buttons & MB_LEFT) {
    if (gEd.Drag == DRAG_TEXT) {
      TextDrag (Ev->X, Ev->Y);
    } else {
      ThumbDrag (Ev->X, Ev->Y);
    }
  }
}

/**
  Fingerprint of what the mouse is over that changes how the screen looks.
**/
STATIC
UINTN
HoverKey (
  VOID
  )
{
  UINTN  Hit;
  INTN   Ty, Th, Hy, Hh;
  UINTN  Part;

  if (!gInput.HasMouse) {
    return 0;
  }

  Hit  = ViewHitTest (gInput.MouseX, gInput.MouseY);
  Part = 0;
  if (Hit == HIT_MENUBAR) {
    Part = (UINTN)(MenuTitleAt (gInput.MouseX, gInput.MouseY) + 2);
  } else if ((Hit == HIT_TEXT) && (gEd.OpenMenu >= 0)) {
    Part = (UINTN)(MenuItemAt ((UINTN)gEd.OpenMenu, gInput.MouseX, gInput.MouseY) + 3) * 64 +
           (UINTN)(SubItemAt (gInput.MouseX, gInput.MouseY) + 3);
  } else if (Hit == HIT_VSCROLL) {
    ViewVScrollGeom (&Ty, &Th, &Hy, &Hh);
    Part = (gInput.MouseY < Ty) ? 1 : (gInput.MouseY >= Ty + Th) ? 2 :
           (gInput.MouseY >= Hy && gInput.MouseY < Hy + Hh) ? 3 : 4;
  } else if (Hit == HIT_HSCROLL) {
    ViewHScrollGeom (&Ty, &Th, &Hy, &Hh);
    Part = (gInput.MouseX < Ty) ? 1 : (gInput.MouseX >= Ty + Th) ? 2 :
           (gInput.MouseX >= Hy && gInput.MouseX < Hy + Hh) ? 3 : 4;
  }

  return Hit * 100 + Part;
}

//
// ---------------- Entry ----------------
//
STATIC
VOID
MainLoop (
  VOID
  )
{
  INPUT_EVENT  Ev;
  BOOLEAN      Dirty;
  UINTN        Hk;
  BOOLEAN      OldCaret;

  FullRedraw ();
  while (!gEd.Quit) {
    UpdateMouseCursor ();
    InputWait (&Ev);
    Dirty = FALSE;

    switch (Ev.Type) {
      case EV_KEY:
        if (gEd.OpenMenu >= 0) {
          MenuKey (&Ev);
        } else if (gEd.MenuKeyboard) {
          //
          // F10 pressed with no popup: arrows move between titles.
          //
          CloseMenu ();
          EditorKey (&Ev);
        } else {
          EditorKey (&Ev);
        }

        gEd.CaretOn = TRUE;
        mLastBlink  = (UINTN)gInput.Ticks;
        Dirty       = TRUE;
        break;

      case EV_MOUSE:
        {
          UINTN  OldTop, OldLeft, OldMenu, OldHot;
          INTN   OldSub, OldSubHot;
          POS    OldCur, OldAnc;

          OldTop  = gEd.TopLine;
          OldLeft = gEd.LeftCol;
          OldMenu = (UINTN)gEd.OpenMenu;
          OldHot  = (UINTN)gEd.MenuHot;
          OldSub    = gEd.SubOpen;
          OldSubHot = gEd.SubHot;
          OldCur  = gEd.Cursor;
          OldAnc  = gEd.Anchor;
          HandleMouse (&Ev);
          Hk = HoverKey ();
          if ((OldTop != gEd.TopLine) || (OldLeft != gEd.LeftCol) ||
              (OldMenu != (UINTN)gEd.OpenMenu) || (OldHot != (UINTN)gEd.MenuHot) ||
              (OldSub != gEd.SubOpen) || (OldSubHot != gEd.SubHot) ||
              (PosCmp (OldCur, gEd.Cursor) != 0) || (PosCmp (OldAnc, gEd.Anchor) != 0) ||
              (Hk != mHoverKey) || (Ev.Pressed != 0) || (Ev.Released != 0))
          {
            Dirty = TRUE;
          }

          mHoverKey = Hk;
        }
        break;

      case EV_TIMER:
        //
        // Keep auto-scrolling while a text drag is held outside the view.
        //
        if ((gEd.Drag == DRAG_TEXT) && (gInput.Buttons & MB_LEFT) &&
            ((gInput.MouseY < gEd.TextY) || (gInput.MouseY >= gEd.TextY + gEd.TextH)) &&
            ((gInput.Ticks & 1) == 0))
        {
          TextDrag (gInput.MouseX, gInput.MouseY);
          Dirty = TRUE;
        }

        OldCaret = gEd.CaretOn;
        if ((UINTN)gInput.Ticks - mLastBlink >= BLINK_TICKS) {
          mLastBlink  = (UINTN)gInput.Ticks;
          gEd.CaretOn = !gEd.CaretOn;
        }

        if (OldCaret != gEd.CaretOn) {
          Dirty = TRUE;
        }

        break;

      default:
        break;
    }

    if (Dirty && !gEd.Quit) {
      FullRedraw ();
    }
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *Params;
  CONST CHAR16                   *Arg;

  Arg    = NULL;
  Status = gBS->HandleProtocol (ImageHandle, &gEfiShellParametersProtocolGuid, (VOID **)&Params);
  if (!EFI_ERROR (Status) && (Params->Argc > 1)) {
    Arg = Params->Argv[1];
    if ((StrCmp (Arg, L"-h") == 0) || (StrCmp (Arg, L"-?") == 0) || (StrCmp (Arg, L"--help") == 0)) {
      Print (L"uefiNotepad %s - Notepad-style text editor for the UEFI Shell\n", NP_VERSION);
      Print (L"Usage: uefinotepad [file]\n");
      return EFI_SUCCESS;
    }
  }

  Status = GfxInit ();
  if (EFI_ERROR (Status)) {
    Print (L"uefiNotepad: no graphics output available (%r)\n", Status);
    return Status;
  }

  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  FileIoInit (ImageHandle);
  EdInit ();
  InputInit ();
  ViewLayout ();

  if (Arg != NULL) {
    if (FileExists (Arg)) {
      OpenPath (Arg, FALSE);
    } else {
      StrCpyS (gEd.Path, MAX_PATH, Arg);
      EdSetMessage (L"New file.");
    }
  }

  MainLoop ();

  InputShutdown ();
  EdFree ();
  GfxShutdown ();
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, TRUE);
  return EFI_SUCCESS;
}
