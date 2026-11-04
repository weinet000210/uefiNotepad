/** @file
  Document model: an array of lines, each a growable CHAR16 buffer.
  Text passed in and out uses L'\n' as the only line separator.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

STATIC
VOID
LineReserve (
  LINE   *L,
  UINTN  Need
  )
{
  UINTN   NewCap;
  CHAR16  *N;

  if (Need <= L->Cap) {
    return;
  }

  NewCap = (L->Cap < 16) ? 16 : L->Cap;
  while (NewCap < Need) {
    NewCap *= 2;
  }

  N = AllocatePool (NewCap * sizeof (CHAR16));
  ASSERT (N != NULL);
  if (N == NULL) {
    return;
  }

  if (L->Text != NULL) {
    CopyMem (N, L->Text, L->Len * sizeof (CHAR16));
    FreePool (L->Text);
  }

  L->Text = N;
  L->Cap  = NewCap;
}

STATIC
VOID
LineInsert (
  LINE          *L,
  UINTN         Col,
  CONST CHAR16  *S,
  UINTN         N
  )
{
  if (N == 0) {
    return;
  }

  if (Col > L->Len) {
    Col = L->Len;
  }

  LineReserve (L, L->Len + N);
  CopyMem (L->Text + Col + N, L->Text + Col, (L->Len - Col) * sizeof (CHAR16));
  CopyMem (L->Text + Col, S, N * sizeof (CHAR16));
  L->Len += N;
}

STATIC
VOID
DocReserve (
  DOC    *Doc,
  UINTN  Need
  )
{
  UINTN  NewCap;
  LINE   *N;

  if (Need <= Doc->Cap) {
    return;
  }

  NewCap = (Doc->Cap < 64) ? 64 : Doc->Cap;
  while (NewCap < Need) {
    NewCap *= 2;
  }

  N = AllocateZeroPool (NewCap * sizeof (LINE));
  ASSERT (N != NULL);
  if (N == NULL) {
    return;
  }

  if (Doc->Lines != NULL) {
    CopyMem (N, Doc->Lines, Doc->Count * sizeof (LINE));
    FreePool (Doc->Lines);
  }

  Doc->Lines = N;
  Doc->Cap   = NewCap;
}

/**
  Insert N empty lines before index At.
**/
STATIC
VOID
DocInsertLines (
  DOC    *Doc,
  UINTN  At,
  UINTN  N
  )
{
  DocReserve (Doc, Doc->Count + N);
  CopyMem (&Doc->Lines[At + N], &Doc->Lines[At], (Doc->Count - At) * sizeof (LINE));
  ZeroMem (&Doc->Lines[At], N * sizeof (LINE));
  Doc->Count += N;
}

STATIC
VOID
DocRemoveLines (
  DOC    *Doc,
  UINTN  At,
  UINTN  N
  )
{
  UINTN  I;

  for (I = At; I < At + N; I++) {
    if (Doc->Lines[I].Text != NULL) {
      FreePool (Doc->Lines[I].Text);
    }
  }

  CopyMem (&Doc->Lines[At], &Doc->Lines[At + N], (Doc->Count - At - N) * sizeof (LINE));
  Doc->Count -= N;
  ZeroMem (&Doc->Lines[Doc->Count], N * sizeof (LINE));
}

VOID
DocInit (
  DOC  *Doc
  )
{
  ZeroMem (Doc, sizeof (*Doc));
  DocReserve (Doc, 64);
  Doc->Count = 1;
}

VOID
DocFree (
  DOC  *Doc
  )
{
  UINTN  I;

  if (Doc->Lines != NULL) {
    for (I = 0; I < Doc->Count; I++) {
      if (Doc->Lines[I].Text != NULL) {
        FreePool (Doc->Lines[I].Text);
      }
    }

    FreePool (Doc->Lines);
  }

  ZeroMem (Doc, sizeof (*Doc));
}

INTN
PosCmp (
  POS  A,
  POS  B
  )
{
  if (A.Line != B.Line) {
    return (A.Line < B.Line) ? -1 : 1;
  }

  if (A.Col != B.Col) {
    return (A.Col < B.Col) ? -1 : 1;
  }

  return 0;
}

POS
PosAdvance (
  POS           At,
  CONST CHAR16  *Str,
  UINTN         Len
  )
{
  UINTN  I;

  for (I = 0; I < Len; I++) {
    if (Str[I] == L'\n') {
      At.Line++;
      At.Col = 0;
    } else {
      At.Col++;
    }
  }

  return At;
}

POS
DocEndPos (
  DOC  *Doc
  )
{
  POS  P;

  P.Line = Doc->Count - 1;
  P.Col  = Doc->Lines[P.Line].Len;
  return P;
}

VOID
DocInsert (
  DOC           *Doc,
  POS           At,
  CONST CHAR16  *Str,
  UINTN         Len,
  POS           *End
  )
{
  UINTN   Breaks;
  UINTN   I, Start, LineIdx;
  LINE    *L;
  CHAR16  *Tail;
  UINTN   TailLen;

  if (At.Line >= Doc->Count) {
    At = DocEndPos (Doc);
  }

  if (At.Col > Doc->Lines[At.Line].Len) {
    At.Col = Doc->Lines[At.Line].Len;
  }

  Breaks = 0;
  for (I = 0; I < Len; I++) {
    if (Str[I] == L'\n') {
      Breaks++;
    }
  }

  if (Breaks == 0) {
    LineInsert (&Doc->Lines[At.Line], At.Col, Str, Len);
    if (End != NULL) {
      End->Line = At.Line;
      End->Col  = At.Col + Len;
    }

    return;
  }

  //
  // Cut the tail of the current line, then lay the new text out over
  // the current line plus Breaks new lines and re-attach the tail.
  //
  L       = &Doc->Lines[At.Line];
  TailLen = L->Len - At.Col;
  Tail    = NULL;
  if (TailLen > 0) {
    Tail = AllocateCopyPool (TailLen * sizeof (CHAR16), L->Text + At.Col);
  }

  L->Len = At.Col;
  DocInsertLines (Doc, At.Line + 1, Breaks);

  LineIdx = At.Line;
  Start   = 0;
  for (I = 0; I <= Len; I++) {
    if ((I == Len) || (Str[I] == L'\n')) {
      LineInsert (&Doc->Lines[LineIdx], Doc->Lines[LineIdx].Len, Str + Start, I - Start);
      if (I < Len) {
        LineIdx++;
      }

      Start = I + 1;
    }
  }

  if (End != NULL) {
    End->Line = LineIdx;
    End->Col  = Doc->Lines[LineIdx].Len;
  }

  if (Tail != NULL) {
    LineInsert (&Doc->Lines[LineIdx], Doc->Lines[LineIdx].Len, Tail, TailLen);
    FreePool (Tail);
  }
}

VOID
DocDelete (
  DOC  *Doc,
  POS  Start,
  POS  End
  )
{
  LINE  *A;
  LINE  *B;

  if (PosCmp (Start, End) > 0) {
    POS  T;

    T     = Start;
    Start = End;
    End   = T;
  }

  if (End.Line >= Doc->Count) {
    End = DocEndPos (Doc);
  }

  A = &Doc->Lines[Start.Line];
  if (Start.Line == End.Line) {
    if (End.Col > A->Len) {
      End.Col = A->Len;
    }

    if (Start.Col >= End.Col) {
      return;
    }

    CopyMem (A->Text + Start.Col, A->Text + End.Col, (A->Len - End.Col) * sizeof (CHAR16));
    A->Len -= End.Col - Start.Col;
    return;
  }

  B = &Doc->Lines[End.Line];
  if (End.Col > B->Len) {
    End.Col = B->Len;
  }

  A->Len = MIN (Start.Col, A->Len);
  LineInsert (A, A->Len, B->Text + End.Col, B->Len - End.Col);
  DocRemoveLines (Doc, Start.Line + 1, End.Line - Start.Line);
}

CHAR16 *
DocGetText (
  DOC    *Doc,
  POS    Start,
  POS    End,
  UINTN  *OutLen
  )
{
  UINTN   Len, L, From, To;
  CHAR16  *Buf;
  CHAR16  *P;

  if (PosCmp (Start, End) > 0) {
    POS  T;

    T     = Start;
    Start = End;
    End   = T;
  }

  Len = 0;
  for (L = Start.Line; L <= End.Line && L < Doc->Count; L++) {
    From = (L == Start.Line) ? Start.Col : 0;
    To   = (L == End.Line) ? End.Col : Doc->Lines[L].Len;
    To   = MIN (To, Doc->Lines[L].Len);
    if (To > From) {
      Len += To - From;
    }

    if (L != End.Line) {
      Len++;
    }
  }

  Buf = AllocatePool ((Len + 1) * sizeof (CHAR16));
  if (Buf == NULL) {
    *OutLen = 0;
    return NULL;
  }

  P = Buf;
  for (L = Start.Line; L <= End.Line && L < Doc->Count; L++) {
    From = (L == Start.Line) ? Start.Col : 0;
    To   = (L == End.Line) ? End.Col : Doc->Lines[L].Len;
    To   = MIN (To, Doc->Lines[L].Len);
    if (To > From) {
      CopyMem (P, Doc->Lines[L].Text + From, (To - From) * sizeof (CHAR16));
      P += To - From;
    }

    if (L != End.Line) {
      *P++ = L'\n';
    }
  }

  *P      = 0;
  *OutLen = (UINTN)(P - Buf);
  return Buf;
}
