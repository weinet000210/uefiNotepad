/** @file
  Open / Save As file browser: volumes on the left, folder contents on the
  right, a file name field, and (Save As) an encoding drop-down.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

#define FD_TITLE_H   26
#define FD_PAD       12
#define FD_DRIVE_W   248
#define FD_DRIVE_H   40
#define FD_ROW_H     20
#define FD_HDR_H     22
#define FD_SB_W      14
#define FD_BTN_W     100
#define FD_BTN_H     26
#define FD_LABEL_W   (11 * FONT_W)
#define FD_COMBO_W   200

// Focus order
#define F_DRIVES  0
#define F_LIST    1
#define F_NAME    2
#define F_COMBO   3
#define F_OK      4
#define F_CANCEL  5

typedef struct {
  BOOLEAN        Save;
  CHAR16         Dir[MAX_PATH];         // always ends with '\'
  VOLUME_INFO    Vols[MAX_VOLUMES];
  UINTN          VolCount;
  INTN           VolSel;
  DIR_ENTRY      *Ent;
  UINTN          Count;
  INTN           Sel;
  UINTN          Top;
  CHAR16         NameBuf[MAX_PATH];
  DLG_FIELD      Name;
  UINTN          Enc;
  BOOLEAN        ComboOpen;
  UINTN          Focus;
  INTN           Pressed;               // F_OK / F_CANCEL / 100 = Up, -1 none
  BOOLEAN        ThumbDrag;
  INTN           ThumbOff;
  CHAR16         Error[64];
  // geometry
  INTN           X, Y, W, H;
  INTN           PanelY, PanelH;
  INTN           ListX, ListW;
  INTN           Row1Y, Row2Y;
} FDLG;

//
// ---------------- Geometry ----------------
//
STATIC
VOID
FdLayout (
  FDLG  *D
  )
{
  D->W      = MIN ((INTN)gGfx.Width - 40, 820);
  D->H      = MIN ((INTN)gGfx.Height - 40, 560);
  D->X      = ((INTN)gGfx.Width - D->W) / 2;
  D->Y      = ((INTN)gGfx.Height - D->H) / 2;
  D->PanelY = D->Y + FD_TITLE_H + 40;
  D->Row2Y  = D->Y + D->H - FD_BTN_H - 12;
  D->Row1Y  = D->Row2Y - FD_BTN_H - 10;
  D->PanelH = D->Row1Y - 14 - D->PanelY;
  D->ListX  = D->X + FD_PAD + FD_DRIVE_W + 8;
  D->ListW  = D->X + D->W - FD_PAD - D->ListX;
}

STATIC
UINTN
VisibleRows (
  FDLG  *D
  )
{
  INTN  N;

  N = (D->PanelH - FD_HDR_H - 2) / FD_ROW_H;
  return (N > 0) ? (UINTN)N : 1;
}

STATIC
BOOLEAN
InRect (
  INTN  Px,
  INTN  Py,
  INTN  X,
  INTN  Y,
  INTN  W,
  INTN  H
  )
{
  return (Px >= X) && (Px < X + W) && (Py >= Y) && (Py < Y + H);
}

STATIC
VOID
UpRect (
  FDLG  *D,
  INTN  *X,
  INTN  *Y,
  INTN  *W,
  INTN  *H
  )
{
  *W = 6 * FONT_W;
  *H = 24;
  *X = D->X + D->W - FD_PAD - *W;
  *Y = D->Y + FD_TITLE_H + 8;
}

STATIC
VOID
NameRect (
  FDLG  *D,
  INTN  *X,
  INTN  *Y,
  INTN  *W
  )
{
  *X = D->X + FD_PAD + FD_LABEL_W;
  *Y = D->Row1Y + 1;
  *W = D->X + D->W - FD_PAD - FD_BTN_W - 10 - *X;
}

STATIC
VOID
ComboRect (
  FDLG  *D,
  INTN  *X,
  INTN  *Y,
  INTN  *W,
  INTN  *H
  )
{
  *X = D->X + FD_PAD + FD_LABEL_W;
  *Y = D->Row2Y + 1;
  *W = FD_COMBO_W;
  *H = DLG_FIELD_H;
}

STATIC
VOID
BtnRect (
  FDLG   *D,
  UINTN  Which,
  INTN   *X,
  INTN   *Y
  )
{
  *X = D->X + D->W - FD_PAD - FD_BTN_W;
  *Y = (Which == F_OK) ? D->Row1Y : D->Row2Y;
}

//
// ---------------- Paths ----------------
//
STATIC
VOID
EnsureSlash (
  CHAR16  *P,
  UINTN   Len
  )
{
  UINTN  N;

  N = StrLen (P);
  if ((N > 0) && (P[N - 1] != L'\\') && (N + 1 < Len)) {
    P[N]     = L'\\';
    P[N + 1] = 0;
  }
}

STATIC
CHAR16 *
LastSep (
  CHAR16  *P
  )
{
  CHAR16  *L;

  L = NULL;
  for ( ; *P != 0; P++) {
    if ((*P == L'\\') || (*P == L'/') || (*P == L':')) {
      L = P;
    }
  }

  return L;
}

STATIC
VOID
SyncVolSel (
  FDLG  *D
  )
{
  UINTN  I, N, K;

  D->VolSel = -1;
  for (I = 0; I < D->VolCount; I++) {
    N = StrLen (D->Vols[I].Name);
    for (K = 0; K < N; K++) {
      if ((D->Dir[K] | 0x20) != (D->Vols[I].Name[K] | 0x20)) {
        break;
      }
    }

    if (K == N) {
      D->VolSel = (INTN)I;
      return;
    }
  }
}

STATIC
VOID
LoadDir (
  FDLG  *D
  )
{
  EFI_STATUS  Status;

  if (D->Ent != NULL) {
    FreePool (D->Ent);
    D->Ent = NULL;
  }

  D->Count    = 0;
  D->Sel      = -1;
  D->Top      = 0;
  D->Error[0] = 0;
  SyncVolSel (D);
  Status = FileListDir (D->Dir, &D->Ent, &D->Count);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (D->Error, sizeof (D->Error), L"Cannot read this folder (%r).", Status);
  }
}

STATIC
VOID
GoUp (
  FDLG  *D
  )
{
  UINTN  N;

  N = StrLen (D->Dir);
  if ((N >= 2) && (D->Dir[N - 1] == L'\\') && (D->Dir[N - 2] != L':')) {
    D->Dir[N - 1] = 0;
    LastSep (D->Dir)[1] = 0;
    LoadDir (D);
  }
}

STATIC
VOID
EnterDir (
  FDLG          *D,
  CONST CHAR16  *Name
  )
{
  if (StrLen (D->Dir) + StrLen (Name) + 2 >= MAX_PATH) {
    return;
  }

  StrCatS (D->Dir, MAX_PATH, Name);
  EnsureSlash (D->Dir, MAX_PATH);
  LoadDir (D);
}

STATIC
VOID
SelectVolume (
  FDLG   *D,
  UINTN  I
  )
{
  if (I >= D->VolCount) {
    return;
  }

  UnicodeSPrint (D->Dir, sizeof (D->Dir), L"%s\\", D->Vols[I].Name);
  LoadDir (D);
}

STATIC
VOID
SetName (
  FDLG          *D,
  CONST CHAR16  *S
  )
{
  StrnCpyS (D->NameBuf, MAX_PATH, S, MAX_PATH - 1);
  D->Name.Cursor = StrLen (D->NameBuf);
  D->Name.Scroll = 0;
  D->Name.AllSel = FALSE;
}

STATIC
VOID
EnsureSelVisible (
  FDLG  *D
  )
{
  UINTN  Vis;

  Vis = VisibleRows (D);
  if (D->Sel < 0) {
    return;
  }

  if ((UINTN)D->Sel < D->Top) {
    D->Top = (UINTN)D->Sel;
  } else if ((UINTN)D->Sel >= D->Top + Vis) {
    D->Top = (UINTN)D->Sel - Vis + 1;
  }
}

STATIC
VOID
ScrollList (
  FDLG  *D,
  INTN  Delta
  )
{
  INTN  Top, Max;

  Max = (INTN)D->Count - (INTN)VisibleRows (D);
  if (Max < 0) {
    Max = 0;
  }

  Top = (INTN)D->Top + Delta;
  Top = MAX (0, MIN (Top, Max));
  D->Top = (UINTN)Top;
}

STATIC
VOID
SetSel (
  FDLG  *D,
  INTN  Sel
  )
{
  if (D->Count == 0) {
    D->Sel = -1;
    return;
  }

  Sel    = MAX (0, MIN (Sel, (INTN)D->Count - 1));
  D->Sel = Sel;
  if (!D->Ent[Sel].Dir) {
    SetName (D, D->Ent[Sel].Name);
  }

  EnsureSelVisible (D);
}

STATIC
BOOLEAN
SameName (
  CONST CHAR16  *A,
  CONST CHAR16  *B
  )
{
  for ( ; *A != 0 && *B != 0; A++, B++) {
    if ((*A | 0x20) != (*B | 0x20)) {
      return FALSE;
    }
  }

  return *A == *B;
}

/**
  Act on the file name field / selection. Returns TRUE when Out holds the
  chosen full path and the dialog should close.
**/
STATIC
BOOLEAN
Accept (
  FDLG    *D,
  CHAR16  *Out,
  UINTN   OutLen
  )
{
  CHAR16  *Name;
  UINTN   I, N;
  CHAR16  Full[MAX_PATH];

  //
  // Trim blanks.
  //
  Name = D->NameBuf;
  while (*Name == L' ') {
    Name++;
  }

  N = StrLen (Name);
  while (N > 0 && Name[N - 1] == L' ') {
    Name[--N] = 0;
  }

  if (N == 0) {
    if ((D->Sel >= 0) && D->Ent[D->Sel].Dir) {
      EnterDir (D, D->Ent[D->Sel].Name);
    }

    return FALSE;
  }

  if (StrCmp (Name, L"..") == 0) {
    GoUp (D);
    SetName (D, L"");
    return FALSE;
  }

  //
  // A folder in the current listing: open it.
  //
  for (I = 0; I < D->Count; I++) {
    if (D->Ent[I].Dir && SameName (D->Ent[I].Name, Name)) {
      EnterDir (D, D->Ent[I].Name);
      SetName (D, L"");
      return FALSE;
    }
  }

  //
  // Build the full path: "FS1:\x.txt", "\dir\x.txt" or a name in Dir.
  //
  Full[0] = 0;
  for (I = 0; Name[I] != 0 && Name[I] != L':'; I++) {
  }

  if (Name[I] == L':') {
    StrnCpyS (Full, MAX_PATH, Name, MAX_PATH - 1);
  } else if ((Name[0] == L'\\') || (Name[0] == L'/')) {
    if (D->VolSel >= 0) {
      UnicodeSPrint (Full, sizeof (Full), L"%s%s", D->Vols[D->VolSel].Name, Name);
    } else {
      StrnCpyS (Full, MAX_PATH, Name, MAX_PATH - 1);
    }
  } else {
    UnicodeSPrint (Full, sizeof (Full), L"%s%s", D->Dir, Name);
  }

  for (I = 0; Full[I] != 0; I++) {
    if (Full[I] == L'/') {
      Full[I] = L'\\';
    }
  }

  //
  // A path ending in '\' is a folder to browse to.
  //
  N = StrLen (Full);
  if ((N > 0) && (Full[N - 1] == L'\\')) {
    StrCpyS (D->Dir, MAX_PATH, Full);
    LoadDir (D);
    SetName (D, L"");
    return FALSE;
  }

  if (!D->Save && !FileExists (Full)) {
    UnicodeSPrint (D->Error, sizeof (D->Error), L"File not found: %s", Name);
    return FALSE;
  }

  StrnCpyS (Out, OutLen, Full, OutLen - 1);
  return TRUE;
}

//
// ---------------- Drawing ----------------
//
STATIC
VOID
FmtSize (
  UINT64  B,
  CHAR16  *Buf,
  UINTN   Size
  )
{
  if (B < 1024) {
    UnicodeSPrint (Buf, Size, L"%Ld B", B);
  } else if (B < 1024 * 1024) {
    UnicodeSPrint (Buf, Size, L"%Ld KB", DivU64x32 (B + 1023, 1024));
  } else if (B < 1024ULL * 1024 * 1024) {
    UnicodeSPrint (Buf, Size, L"%Ld MB", DivU64x32 (B, 1024 * 1024));
  } else {
    UINT64  Tenths;

    Tenths = DivU64x32 (B, 1024 * 1024 * 1024 / 10);
    UnicodeSPrint (Buf, Size, L"%Ld.%Ld GB", DivU64x32 (Tenths, 10), Tenths % 10);
  }
}

STATIC
VOID
DrawButton (
  INTN          X,
  INTN          Y,
  INTN          W,
  CONST CHAR16  *Label,
  BOOLEAN       Focused,
  BOOLEAN       Pressed
  )
{
  BOOLEAN  Hot;

  Hot = gInput.HasMouse && InRect (gInput.MouseX, gInput.MouseY, X, Y, W, FD_BTN_H);
  GfxFillRect (X, Y, W, FD_BTN_H, Pressed ? 0xCCE4F7 : (Hot ? 0xE5F1FB : C_BTN));
  GfxFrameRect (X, Y, W, FD_BTN_H, (Focused || Hot) ? C_HILITE : C_BORDER);
  if (Focused) {
    GfxFrameRect (X + 1, Y + 1, W - 2, FD_BTN_H - 2, C_HILITE);
  }

  GfxDrawText (X + (W - (INTN)StrLen (Label) * FONT_W) / 2, Y + (FD_BTN_H - FONT_H) / 2, Label, C_TEXT);
}

STATIC
VOID
DrawFolderIcon (
  INTN  X,
  INTN  Y
  )
{
  GfxFillRect (X, Y + 2, 6, 3, 0xD9A53A);
  GfxFillRect (X, Y + 4, 14, 10, 0xF2C55C);
  GfxFrameRect (X, Y + 4, 14, 10, 0xD9A53A);
}

STATIC
VOID
DrawFileIcon (
  INTN  X,
  INTN  Y
  )
{
  GfxFillRect (X + 1, Y, 11, 15, C_WINDOW);
  GfxFrameRect (X + 1, Y, 11, 15, 0x8C8C8C);
  GfxHLine (X + 3, Y + 4, 7, 0x9DB7D9);
  GfxHLine (X + 3, Y + 7, 7, 0x9DB7D9);
  GfxHLine (X + 3, Y + 10, 5, 0x9DB7D9);
}

STATIC
VOID
DrawDrives (
  FDLG  *D
  )
{
  INTN    X, Y, W;
  UINTN   I;
  CHAR16  Line[64];
  CHAR16  Free[24];
  CHAR16  Total[24];
  BOOLEAN Sel;
  UINT32  Fg, Fg2;

  X = D->X + FD_PAD;
  W = FD_DRIVE_W;
  GfxFillRect (X, D->PanelY, W, D->PanelH, C_WINDOW);
  GfxFrameRect (X, D->PanelY, W, D->PanelH, (D->Focus == F_DRIVES) ? C_HILITE : 0xADADAD);
  GfxSetClip (X + 1, D->PanelY + 1, X + W - 1, D->PanelY + D->PanelH - 1);

  for (I = 0; I < D->VolCount; I++) {
    Y   = D->PanelY + 2 + (INTN)I * FD_DRIVE_H;
    Sel = (D->VolSel == (INTN)I);
    Fg  = C_TEXT;
    Fg2 = C_GRAYTEXT;
    if (Sel) {
      GfxFillRect (X + 2, Y, W - 4, FD_DRIVE_H - 2, (D->Focus == F_DRIVES) ? C_HILITE : 0xD9EBF9);
      if (D->Focus == F_DRIVES) {
        Fg  = C_HILITE_FG;
        Fg2 = 0xDDEEFF;
      }
    }

    //
    // Drive icon.
    //
    GfxFillRect (X + 8, Y + 9, 18, 12, 0x9A9A9A);
    GfxFillRect (X + 10, Y + 11, 14, 4, 0xD0D0D0);
    GfxFillRect (X + 21, Y + 17, 3, 2, 0x3CC33C);

    UnicodeSPrint (Line, sizeof (Line), L"%s %s", D->Vols[I].Name, D->Vols[I].Label);
    GfxDrawText (X + 34, Y + 1, Line, Fg);
    if (D->Vols[I].Size > 0) {
      FmtSize (D->Vols[I].Free, Free, sizeof (Free));
      FmtSize (D->Vols[I].Size, Total, sizeof (Total));
      UnicodeSPrint (Line, sizeof (Line), L"%s free of %s", Free, Total);
    } else {
      Line[0] = 0;
    }

    if (D->Vols[I].ReadOnly) {
      StrCatS (Line, ARRAY_SIZE (Line), L" (RO)");
    }

    GfxDrawText (X + 34, Y + 19, Line, Fg2);
  }

  if (D->VolCount == 0) {
    GfxDrawText (X + 8, D->PanelY + 8, L"No file systems", C_GRAYTEXT);
  }

  GfxResetClip ();
}

STATIC
VOID
ListScrollGeom (
  FDLG  *D,
  INTN  *TrackY,
  INTN  *TrackH,
  INTN  *ThumbY,
  INTN  *ThumbH
  )
{
  UINTN  Vis, Max;

  Vis     = VisibleRows (D);
  *TrackY = D->PanelY + FD_HDR_H;
  *TrackH = D->PanelH - FD_HDR_H - 1;
  if (D->Count <= Vis) {
    *ThumbY = *TrackY;
    *ThumbH = *TrackH;
    return;
  }

  Max     = D->Count - Vis;
  *ThumbH = MAX (20, (INTN)((UINTN)*TrackH * Vis / D->Count));
  *ThumbY = *TrackY + (INTN)((UINTN)(*TrackH - *ThumbH) * MIN (D->Top, Max) / Max);
}

STATIC
VOID
DrawList (
  FDLG  *D
  )
{
  INTN       X, Y, W, SizeX, DateX, NameW;
  UINTN      I, Vis;
  DIR_ENTRY  *E;
  BOOLEAN    Sel;
  UINT32     Fg;
  CHAR16     Buf[32];
  INTN       Ty, Th, Hy, Hh;

  X     = D->ListX;
  W     = D->ListW;
  DateX = X + W - FD_SB_W - 17 * FONT_W - 6;
  SizeX = DateX - 11 * FONT_W;
  NameW = SizeX - X - 30;

  GfxFillRect (X, D->PanelY, W, D->PanelH, C_WINDOW);
  GfxFrameRect (X, D->PanelY, W, D->PanelH, (D->Focus == F_LIST) ? C_HILITE : 0xADADAD);

  //
  // Column header.
  //
  GfxFillRect (X + 1, D->PanelY + 1, W - 2, FD_HDR_H - 1, 0xF7F7F7);
  GfxHLine (X + 1, D->PanelY + FD_HDR_H - 1, W - 2, 0xE5E5E5);
  GfxDrawText (X + 28, D->PanelY + 2, L"Name", C_GRAYTEXT);
  GfxDrawText (SizeX + 3 * FONT_W, D->PanelY + 2, L"Size", C_GRAYTEXT);
  GfxDrawText (DateX, D->PanelY + 2, L"Modified", C_GRAYTEXT);

  Vis = VisibleRows (D);
  GfxSetClip (X + 1, D->PanelY + FD_HDR_H, X + W - FD_SB_W - 1, D->PanelY + D->PanelH - 1);
  for (I = D->Top; I < D->Count && I < D->Top + Vis; I++) {
    E   = &D->Ent[I];
    Y   = D->PanelY + FD_HDR_H + 1 + (INTN)(I - D->Top) * FD_ROW_H;
    Sel = (D->Sel == (INTN)I);
    Fg  = C_TEXT;
    if (Sel) {
      GfxFillRect (X + 2, Y, W - FD_SB_W - 4, FD_ROW_H, (D->Focus == F_LIST) ? C_HILITE : 0xD9EBF9);
      if (D->Focus == F_LIST) {
        Fg = C_HILITE_FG;
      }
    }

    if (E->Dir) {
      DrawFolderIcon (X + 8, Y + 2);
    } else {
      DrawFileIcon (X + 8, Y + 2);
    }

    GfxSetClip (X + 1, D->PanelY + FD_HDR_H, X + 28 + NameW, D->PanelY + D->PanelH - 1);
    GfxDrawText (X + 28, Y, E->Name, Fg);
    GfxSetClip (X + 1, D->PanelY + FD_HDR_H, X + W - FD_SB_W - 1, D->PanelY + D->PanelH - 1);

    if (!E->Dir) {
      FmtSize (E->Size, Buf, sizeof (Buf));
      GfxDrawText (SizeX + 9 * FONT_W - (INTN)StrLen (Buf) * FONT_W, Y, Buf, Fg);
    }

    if (E->Modified.Year != 0) {
      UnicodeSPrint (
        Buf,
        sizeof (Buf),
        L"%04d-%02d-%02d %02d:%02d",
        E->Modified.Year,
        E->Modified.Month,
        E->Modified.Day,
        E->Modified.Hour,
        E->Modified.Minute
        );
      GfxDrawText (DateX, Y, Buf, Fg);
    }
  }

  GfxResetClip ();

  if (D->Error[0] != 0) {
    GfxDrawText (X + 12, D->PanelY + FD_HDR_H + 8, D->Error, 0xC42B1C);
  } else if (D->Count == 0) {
    GfxDrawText (X + 12, D->PanelY + FD_HDR_H + 8, L"This folder is empty.", C_GRAYTEXT);
  }

  //
  // Scroll bar.
  //
  ListScrollGeom (D, &Ty, &Th, &Hy, &Hh);
  GfxFillRect (X + W - FD_SB_W - 1, Ty, FD_SB_W, Th, C_FACE);
  if (D->Count > Vis) {
    GfxFillRect (X + W - FD_SB_W + 2, Hy + 1, FD_SB_W - 6, Hh - 2, D->ThumbDrag ? C_THUMB_HOT : C_THUMB);
  }
}

STATIC
VOID
DrawCombo (
  FDLG  *D
  )
{
  INTN  X, Y, W, H;

  ComboRect (D, &X, &Y, &W, &H);
  GfxDrawText (D->X + FD_PAD, Y + (H - FONT_H) / 2, L"Encoding:", C_TEXT);
  GfxFillRect (X, Y, W, H, C_WINDOW);
  GfxFrameRect (X, Y, W, H, (D->Focus == F_COMBO) ? C_HILITE : 0x7A7A7A);
  GfxDrawText (X + 6, Y + (H - FONT_H) / 2, EncName (D->Enc), C_TEXT);
  GfxDrawArrow (X + W - 12, Y + H / 2, 1, 0x404040);
}

STATIC
VOID
DrawComboList (
  FDLG  *D
  )
{
  INTN   X, Y, W, H, Iy, Lh;
  UINTN  I;
  BOOLEAN Hot;

  if (!D->ComboOpen) {
    return;
  }

  ComboRect (D, &X, &Y, &W, &H);
  Lh = ENC_COUNT * FD_ROW_H + 2;
  //
  // Open upwards: the combo sits at the bottom of the dialog.
  //
  Iy = Y - Lh;
  GfxFillRect (X + 3, Iy + 3, W, Lh, 0xA0A0A0);
  GfxFillRect (X, Iy, W, Lh, C_WINDOW);
  GfxFrameRect (X, Iy, W, Lh, C_HILITE);
  for (I = 0; I < ENC_COUNT; I++) {
    Hot = gInput.HasMouse && InRect (gInput.MouseX, gInput.MouseY, X, Iy + 1 + (INTN)I * FD_ROW_H, W, FD_ROW_H);
    if (Hot || (!gInput.HasMouse && (I == D->Enc))) {
      GfxFillRect (X + 1, Iy + 1 + (INTN)I * FD_ROW_H, W - 2, FD_ROW_H, 0x91C9F7);
    }

    GfxDrawText (X + 6, Iy + 1 + (INTN)I * FD_ROW_H, EncName (I), C_TEXT);
    if (I == D->Enc) {
      GfxFillRect (X + W - 16, Iy + 1 + (INTN)I * FD_ROW_H + 8, 5, 5, C_TEXT);
    }
  }
}

STATIC
VOID
FdDraw (
  FDLG  *D
  )
{
  INTN    Ux, Uy, Uw, Uh, Nx, Ny, Nw, Bx, By;

  GfxFillRect (D->X + 4, D->Y + 4, D->W, D->H, 0x808080);
  GfxFillRect (D->X, D->Y, D->W, D->H, C_FACE);
  GfxFillRect (D->X, D->Y, D->W, FD_TITLE_H, C_WINDOW);
  GfxFrameRect (D->X, D->Y, D->W, D->H, C_HILITE);
  GfxDrawText (D->X + 12, D->Y + (FD_TITLE_H - FONT_H) / 2 + 1, D->Save ? L"Save As" : L"Open", C_TEXT);

  //
  // Location bar and Up button.
  //
  UpRect (D, &Ux, &Uy, &Uw, &Uh);
  GfxDrawText (D->X + FD_PAD, Uy + 3, L"Look in:", C_TEXT);
  GfxFillRect (D->X + FD_PAD + FD_LABEL_W, Uy, Ux - 8 - (D->X + FD_PAD + FD_LABEL_W), Uh, C_WINDOW);
  GfxFrameRect (D->X + FD_PAD + FD_LABEL_W, Uy, Ux - 8 - (D->X + FD_PAD + FD_LABEL_W), Uh, 0xADADAD);
  GfxSetClip (D->X + FD_PAD + FD_LABEL_W + 2, Uy, Ux - 10, Uy + Uh);
  GfxDrawText (D->X + FD_PAD + FD_LABEL_W + 6, Uy + 3, D->Dir, C_TEXT);
  GfxResetClip ();
  GfxFillRect (Ux, Uy, Uw, Uh, (D->Pressed == 100) ? 0xCCE4F7 : C_BTN);
  GfxFrameRect (Ux, Uy, Uw, Uh, C_BORDER);
  GfxDrawArrow (Ux + 12, Uy + Uh / 2, 0, 0x404040);
  GfxDrawText (Ux + 22, Uy + 3, L"Up", C_TEXT);

  DrawDrives (D);
  DrawList (D);

  //
  // File name, encoding, buttons.
  //
  NameRect (D, &Nx, &Ny, &Nw);
  GfxDrawText (D->X + FD_PAD, Ny + (DLG_FIELD_H - FONT_H) / 2, L"File name:", C_TEXT);
  DlgFieldDraw (&D->Name, Nx, Ny, Nw, D->Focus == F_NAME);
  if (D->Save) {
    DrawCombo (D);
  }

  BtnRect (D, F_OK, &Bx, &By);
  DrawButton (Bx, By, FD_BTN_W, D->Save ? L"Save" : L"Open", D->Focus == F_OK || D->Focus == F_NAME, D->Pressed == F_OK);
  BtnRect (D, F_CANCEL, &Bx, &By);
  DrawButton (Bx, By, FD_BTN_W, L"Cancel", D->Focus == F_CANCEL, D->Pressed == F_CANCEL);

  DrawComboList (D);
}

STATIC
VOID
FdPresent (
  FDLG  *D
  )
{
  ViewRender ();
  FdDraw (D);
  GfxPresent ();
}

//
// ---------------- Input ----------------
//
#define R_NONE    0
#define R_OK      1
#define R_CANCEL  2

STATIC
UINTN
NextFocus (
  FDLG     *D,
  BOOLEAN  Back
  )
{
  UINTN  F;

  F = D->Focus;
  do {
    F = Back ? (F + 5) % 6 : (F + 1) % 6;
  } while ((F == F_COMBO) && !D->Save);

  return F;
}

STATIC
UINTN
FdKey (
  FDLG         *D,
  INPUT_EVENT  *Ev,
  CHAR16       *Out,
  UINTN        OutLen
  )
{
  BOOLEAN  Enter;
  UINTN    Vis;

  Enter = (Ev->Char == CHAR_CARRIAGE_RETURN) || (Ev->Char == CHAR_LINEFEED);
  Vis   = VisibleRows (D);

  if (D->ComboOpen) {
    if (Ev->Scan == SCAN_UP) {
      D->Enc = (D->Enc + ENC_COUNT - 1) % ENC_COUNT;
    } else if (Ev->Scan == SCAN_DOWN) {
      D->Enc = (D->Enc + 1) % ENC_COUNT;
    } else if (Enter || (Ev->Scan == SCAN_ESC) || (Ev->Char == L' ')) {
      D->ComboOpen = FALSE;
    }

    return R_NONE;
  }

  if (Ev->Scan == SCAN_ESC) {
    return R_CANCEL;
  }

  if (Ev->Char == CHAR_TAB) {
    D->Focus = NextFocus (D, Ev->Shift);
    if (D->Focus == F_NAME) {
      D->Name.AllSel = (D->NameBuf[0] != 0);
      D->Name.Cursor = StrLen (D->NameBuf);
    }

    if ((D->Focus == F_LIST) && (D->Sel < 0) && (D->Count > 0)) {
      SetSel (D, 0);
    }

    return R_NONE;
  }

  switch (D->Focus) {
    case F_DRIVES:
      if ((Ev->Scan == SCAN_UP) && (D->VolSel > 0)) {
        SelectVolume (D, (UINTN)D->VolSel - 1);
      } else if ((Ev->Scan == SCAN_DOWN) && (D->VolSel + 1 < (INTN)D->VolCount)) {
        SelectVolume (D, (UINTN)D->VolSel + 1);
      } else if (Enter || (Ev->Scan == SCAN_RIGHT)) {
        D->Focus = F_LIST;
        if ((D->Sel < 0) && (D->Count > 0)) {
          SetSel (D, 0);
        }
      }

      return R_NONE;

    case F_LIST:
      switch (Ev->Scan) {
        case SCAN_UP:
          SetSel (D, D->Sel - 1);
          return R_NONE;
        case SCAN_DOWN:
          SetSel (D, D->Sel + 1);
          return R_NONE;
        case SCAN_PAGE_UP:
          SetSel (D, D->Sel - (INTN)Vis);
          return R_NONE;
        case SCAN_PAGE_DOWN:
          SetSel (D, D->Sel + (INTN)Vis);
          return R_NONE;
        case SCAN_HOME:
          SetSel (D, 0);
          return R_NONE;
        case SCAN_END:
          SetSel (D, (INTN)D->Count - 1);
          return R_NONE;
        case SCAN_LEFT:
          D->Focus = F_DRIVES;
          return R_NONE;
        default:
          break;
      }

      if (Ev->Char == CHAR_BACKSPACE) {
        GoUp (D);
        return R_NONE;
      }

      if (Enter && (D->Sel >= 0)) {
        if (D->Ent[D->Sel].Dir) {
          EnterDir (D, D->Ent[D->Sel].Name);
          if (D->Count > 0) {
            SetSel (D, 0);
          }

          return R_NONE;
        }

        SetName (D, D->Ent[D->Sel].Name);
        return Accept (D, Out, OutLen) ? R_OK : R_NONE;
      }

      if ((Ev->Char >= 0x20) && !Ev->Ctrl) {
        //
        // Typing jumps to the name field.
        //
        D->Focus       = F_NAME;
        D->Name.AllSel = TRUE;
        DlgFieldKey (&D->Name, Ev);
      }

      return R_NONE;

    case F_NAME:
      if (Enter) {
        return Accept (D, Out, OutLen) ? R_OK : R_NONE;
      }

      if ((Ev->Scan == SCAN_DOWN) || (Ev->Scan == SCAN_UP)) {
        D->Focus = F_LIST;
        if ((D->Sel < 0) && (D->Count > 0)) {
          SetSel (D, 0);
        }

        return R_NONE;
      }

      D->Error[0] = 0;
      DlgFieldKey (&D->Name, Ev);
      return R_NONE;

    case F_COMBO:
      if (Ev->Scan == SCAN_UP) {
        D->Enc = (D->Enc + ENC_COUNT - 1) % ENC_COUNT;
      } else if (Ev->Scan == SCAN_DOWN) {
        D->Enc = (D->Enc + 1) % ENC_COUNT;
      } else if (Enter || (Ev->Char == L' ')) {
        D->ComboOpen = TRUE;
      }

      return R_NONE;

    case F_OK:
      if (Enter || (Ev->Char == L' ')) {
        return Accept (D, Out, OutLen) ? R_OK : R_NONE;
      }

      return R_NONE;

    default:
      if (Enter || (Ev->Char == L' ')) {
        return R_CANCEL;
      }

      return R_NONE;
  }
}

STATIC
UINTN
FdMouse (
  FDLG         *D,
  INPUT_EVENT  *Ev,
  CHAR16       *Out,
  UINTN        OutLen
  )
{
  INTN   X, Y, W, H, Bx, By, Ty, Th, Hy, Hh;
  INTN   Row;
  UINTN  Vis;

  Vis = VisibleRows (D);

  if (Ev->Wheel != 0) {
    ScrollList (D, Ev->Wheel * 3);
  }

  //
  // Thumb drag.
  //
  if (D->ThumbDrag) {
    if (Ev->Buttons & MB_LEFT) {
      ListScrollGeom (D, &Ty, &Th, &Hy, &Hh);
      if ((Th > Hh) && (D->Count > Vis)) {
        D->Top = 0;
        ScrollList (D, (Ev->Y - D->ThumbOff - Ty) * (INTN)(D->Count - Vis) / (Th - Hh));
      }
    } else {
      D->ThumbDrag = FALSE;
    }

    return R_NONE;
  }

  //
  // Button releases.
  //
  if ((Ev->Released & MB_LEFT) && (D->Pressed != -1)) {
    INTN  P;

    P          = D->Pressed;
    D->Pressed = -1;
    if (P == 100) {
      UpRect (D, &X, &Y, &W, &H);
      if (InRect (Ev->X, Ev->Y, X, Y, W, H)) {
        GoUp (D);
      }

      return R_NONE;
    }

    BtnRect (D, (UINTN)P, &Bx, &By);
    if (InRect (Ev->X, Ev->Y, Bx, By, FD_BTN_W, FD_BTN_H)) {
      if (P == F_OK) {
        return Accept (D, Out, OutLen) ? R_OK : R_NONE;
      }

      return R_CANCEL;
    }

    return R_NONE;
  }

  if (!(Ev->Pressed & MB_LEFT)) {
    return R_NONE;
  }

  //
  // Open combo list takes the click first.
  //
  if (D->ComboOpen) {
    ComboRect (D, &X, &Y, &W, &H);
    Ty = Y - (ENC_COUNT * FD_ROW_H + 2);
    if (InRect (Ev->X, Ev->Y, X, Ty + 1, W, ENC_COUNT * FD_ROW_H)) {
      D->Enc = (UINTN)((Ev->Y - Ty - 1) / FD_ROW_H);
    }

    D->ComboOpen = FALSE;
    return R_NONE;
  }

  D->Error[0] = 0;

  //
  // Up button, OK, Cancel.
  //
  UpRect (D, &X, &Y, &W, &H);
  if (InRect (Ev->X, Ev->Y, X, Y, W, H)) {
    D->Pressed = 100;
    return R_NONE;
  }

  BtnRect (D, F_OK, &Bx, &By);
  if (InRect (Ev->X, Ev->Y, Bx, By, FD_BTN_W, FD_BTN_H)) {
    D->Pressed = F_OK;
    D->Focus   = F_OK;
    return R_NONE;
  }

  BtnRect (D, F_CANCEL, &Bx, &By);
  if (InRect (Ev->X, Ev->Y, Bx, By, FD_BTN_W, FD_BTN_H)) {
    D->Pressed = F_CANCEL;
    D->Focus   = F_CANCEL;
    return R_NONE;
  }

  //
  // Drives.
  //
  if (InRect (Ev->X, Ev->Y, D->X + FD_PAD, D->PanelY, FD_DRIVE_W, D->PanelH)) {
    D->Focus = F_DRIVES;
    Row      = (Ev->Y - D->PanelY - 2) / FD_DRIVE_H;
    if ((Row >= 0) && (Row < (INTN)D->VolCount) && (Row != D->VolSel)) {
      SelectVolume (D, (UINTN)Row);
    }

    return R_NONE;
  }

  //
  // File list and its scroll bar.
  //
  if (InRect (Ev->X, Ev->Y, D->ListX, D->PanelY, D->ListW, D->PanelH)) {
    D->Focus = F_LIST;
    if (Ev->X >= D->ListX + D->ListW - FD_SB_W - 1) {
      ListScrollGeom (D, &Ty, &Th, &Hy, &Hh);
      if (Ev->Y < Hy) {
        ScrollList (D, -(INTN)Vis);
      } else if (Ev->Y >= Hy + Hh) {
        ScrollList (D, (INTN)Vis);
      } else {
        D->ThumbDrag = TRUE;
        D->ThumbOff  = Ev->Y - Hy;
      }

      return R_NONE;
    }

    Row = (Ev->Y - D->PanelY - FD_HDR_H - 1);
    if (Row < 0) {
      return R_NONE;
    }

    Row = Row / FD_ROW_H + (INTN)D->Top;
    if (Row >= (INTN)D->Count) {
      D->Sel = -1;
      return R_NONE;
    }

    if (Ev->DoubleClick && (Row == D->Sel)) {
      if (D->Ent[Row].Dir) {
        EnterDir (D, D->Ent[Row].Name);
        return R_NONE;
      }

      SetName (D, D->Ent[Row].Name);
      return Accept (D, Out, OutLen) ? R_OK : R_NONE;
    }

    SetSel (D, Row);
    return R_NONE;
  }

  //
  // Name field.
  //
  NameRect (D, &X, &Y, &W);
  if (InRect (Ev->X, Ev->Y, X, Y, W, DLG_FIELD_H)) {
    INTN  C;

    D->Focus       = F_NAME;
    D->Name.AllSel = FALSE;
    C              = (Ev->X - X - 5 + FONT_W / 2) / FONT_W + (INTN)D->Name.Scroll;
    D->Name.Cursor = (C < 0) ? 0 : MIN ((UINTN)C, StrLen (D->NameBuf));
    return R_NONE;
  }

  //
  // Encoding combo.
  //
  if (D->Save) {
    ComboRect (D, &X, &Y, &W, &H);
    if (InRect (Ev->X, Ev->Y, X, Y, W, H)) {
      D->Focus     = F_COMBO;
      D->ComboOpen = TRUE;
    }
  }

  return R_NONE;
}

BOOLEAN
FileDialog (
  BOOLEAN  Save,
  CHAR16   *Path,
  UINTN    PathLen,
  UINTN    *Enc
  )
{
  FDLG         *D;
  INPUT_EVENT  Ev;
  UINTN        R, LastBlink;
  CHAR16       *Sep;
  BOOLEAN      Ok;

  D = AllocateZeroPool (sizeof (FDLG));
  if (D == NULL) {
    return FALSE;
  }

  D->Save        = Save;
  D->Enc         = *Enc;
  D->Pressed     = -1;
  D->Name.Buf    = D->NameBuf;
  D->Name.BufLen = MAX_PATH;
  D->VolCount    = FileListVolumes (D->Vols, MAX_VOLUMES);

  //
  // Start in the folder of the current file, else the Shell's current one.
  //
  D->Dir[0] = 0;
  if (Path[0] != 0) {
    for (Sep = Path; *Sep != 0 && *Sep != L':'; Sep++) {
    }

    if (*Sep == L':') {
      StrnCpyS (D->Dir, MAX_PATH, Path, MAX_PATH - 1);
    } else {
      FileCurDir (D->Dir, MAX_PATH);
      EnsureSlash (D->Dir, MAX_PATH);
      StrCatS (D->Dir, MAX_PATH, Path);
    }

    for (Sep = D->Dir; *Sep != 0; Sep++) {
      if (*Sep == L'/') {
        *Sep = L'\\';
      }
    }

    Sep = LastSep (D->Dir);
    if (Save) {
      SetName (D, (Sep != NULL) ? Sep + 1 : D->Dir);
    }

    if (Sep != NULL) {
      if (*Sep == L':') {
        Sep[1] = L'\\';
        Sep[2] = 0;
      } else {
        Sep[1] = 0;
      }
    }
  }

  if (D->Dir[0] == 0) {
    FileCurDir (D->Dir, MAX_PATH);
    EnsureSlash (D->Dir, MAX_PATH);
    if (Save) {
      SetName (D, L"Untitled.txt");
    }
  }

  FdLayout (D);
  LoadDir (D);
  if ((D->VolSel < 0) && (D->VolCount > 0)) {
    SelectVolume (D, 0);
  }

  D->Focus       = F_NAME;
  D->Name.AllSel = (D->NameBuf[0] != 0);
  D->Name.Cursor = StrLen (D->NameBuf);

  gModal++;
  gEd.OpenMenu = -1;
  gEd.CaretOn  = TRUE;
  LastBlink    = (UINTN)gInput.Ticks;
  Ok           = FALSE;
  FdPresent (D);

  for ( ; ;) {
    UpdateMouseCursor ();
    InputWait (&Ev);
    R = R_NONE;
    if (Ev.Type == EV_TIMER) {
      if ((UINTN)gInput.Ticks - LastBlink >= 26) {
        LastBlink   = (UINTN)gInput.Ticks;
        gEd.CaretOn = !gEd.CaretOn;
        FdPresent (D);
      }

      continue;
    }

    if (Ev.Type == EV_KEY) {
      gEd.CaretOn = TRUE;
      LastBlink   = (UINTN)gInput.Ticks;
      R           = FdKey (D, &Ev, Path, PathLen);
    } else if (Ev.Type == EV_MOUSE) {
      R = FdMouse (D, &Ev, Path, PathLen);
    }

    if (R == R_OK) {
      *Enc = D->Enc;
      Ok   = TRUE;
      break;
    }

    if (R == R_CANCEL) {
      break;
    }

    FdPresent (D);
  }

  gModal--;
  if (D->Ent != NULL) {
    FreePool (D->Ent);
  }

  FreePool (D);
  FullRedraw ();
  return Ok;
}
