/** @file
  Window layout, rendering, menus and hit testing.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

#define MENU_ITEM_H   22
#define MENU_SEP_H    9
#define MENU_PAD      3

STATIC CONST MENU_ITEM  mFileItems[] = {
  { L"New",        L"Ctrl+N",       CMD_NEW    },
  { L"Open...",    L"Ctrl+O",       CMD_OPEN   },
  { L"Save",       L"Ctrl+S",       CMD_SAVE   },
  { L"Save As...", L"Ctrl+Shift+S", CMD_SAVEAS },
  { NULL,          NULL,            CMD_NONE   },
  { L"Exit",       L"Ctrl+Q",       CMD_EXIT   }
};

STATIC CONST MENU_ITEM  mEditItems[] = {
  { L"Undo",       L"Ctrl+Z", CMD_UNDO      },
  { L"Redo",       L"Ctrl+Y", CMD_REDO      },
  { NULL,          NULL,      CMD_NONE      },
  { L"Cut",        L"Ctrl+X", CMD_CUT       },
  { L"Copy",       L"Ctrl+C", CMD_COPY      },
  { L"Paste",      L"Ctrl+V", CMD_PASTE     },
  { L"Delete",     L"Del",    CMD_DELETE    },
  { NULL,          NULL,      CMD_NONE      },
  { L"Select All", L"Ctrl+A", CMD_SELECTALL },
  { L"Time/Date",  L"F5",     CMD_TIMEDATE  }
};

STATIC CONST MENU_ITEM  mSearchItems[] = {
  { L"Find...",    L"Ctrl+F", CMD_FIND     },
  { L"Find Next",  L"F3",     CMD_FINDNEXT },
  { L"Replace...", L"Ctrl+H", CMD_REPLACE  },
  { L"Go To...",   L"Ctrl+G", CMD_GOTO     }
};

STATIC CONST MENU_ITEM  mFormatItems[] = {
  { L"Encoding",    L"", CMD_ENCODING },
  { L"Line Ending", L"", CMD_EOL      }
};

STATIC CONST MENU_ITEM  mViewItems[] = {
  { L"Status Bar", L"", CMD_STATUSBAR }
};

STATIC CONST MENU_ITEM  mHelpItems[] = {
  { L"Keyboard Shortcuts", L"F1", CMD_KEYS  },
  { L"About uefiNotepad",  L"",   CMD_ABOUT }
};

CONST MENU  gMenus[] = {
  { L"File",   L'f', mFileItems,   ARRAY_SIZE (mFileItems)   },
  { L"Edit",   L'e', mEditItems,   ARRAY_SIZE (mEditItems)   },
  { L"Search", L's', mSearchItems, ARRAY_SIZE (mSearchItems) },
  { L"Format", L'o', mFormatItems, ARRAY_SIZE (mFormatItems) },
  { L"View",   L'v', mViewItems,   ARRAY_SIZE (mViewItems)   },
  { L"Help",   L'h', mHelpItems,   ARRAY_SIZE (mHelpItems)   }
};

CONST UINTN  gMenuCount = ARRAY_SIZE (gMenus);

STATIC CHAR16  mDynLabel[48];

CONST CHAR16 *
MenuItemLabel (
  CONST MENU_ITEM  *Item
  )
{
  switch (Item->Cmd) {
    case CMD_ENCODING:
      UnicodeSPrint (mDynLabel, sizeof (mDynLabel), L"Encoding: %s", EncName (gEd.Enc));
      return mDynLabel;
    case CMD_EOL:
      UnicodeSPrint (mDynLabel, sizeof (mDynLabel), L"Line Ending: %s", (gEd.Eol == EOL_CRLF) ? L"CRLF" : L"LF");
      return mDynLabel;
    default:
      return Item->Label;
  }
}

STATIC
BOOLEAN
MenuItemEnabled (
  CONST MENU_ITEM  *Item
  )
{
  switch (Item->Cmd) {
    case CMD_UNDO:
      return gEd.Undo.Count > 0;
    case CMD_REDO:
      return gEd.Redo.Count > 0;
    case CMD_CUT:
    case CMD_COPY:
    case CMD_DELETE:
      return EdHasSel ();
    case CMD_PASTE:
      return gEd.ClipLen > 0;
    case CMD_FINDNEXT:
      return gEd.FindText[0] != 0;
    default:
      return TRUE;
  }
}

INTN
MenuTitleX (
  UINTN  Index
  )
{
  INTN   X;
  UINTN  I;

  X = 2;
  for (I = 0; I < Index; I++) {
    X += MenuTitleW (I);
  }

  return X;
}

INTN
MenuTitleW (
  UINTN  Index
  )
{
  return (INTN)(StrLen (gMenus[Index].Title) + 2) * FONT_W;
}

VOID
MenuPopupRect (
  UINTN  Index,
  INTN   *X,
  INTN   *Y,
  INTN   *W,
  INTN   *H
  )
{
  CONST MENU  *M;
  UINTN       I, Lw, Aw, MaxL, MaxA;

  M    = &gMenus[Index];
  MaxL = 0;
  MaxA = 0;
  *H   = MENU_PAD * 2;
  for (I = 0; I < M->Count; I++) {
    if (M->Items[I].Label == NULL) {
      *H += MENU_SEP_H;
      continue;
    }

    *H += MENU_ITEM_H;
    Lw  = StrLen (MenuItemLabel (&M->Items[I]));
    Aw  = StrLen (M->Items[I].Accel);
    MaxL = MAX (MaxL, Lw);
    MaxA = MAX (MaxA, Aw);
  }

  // Fixed room for the longest dynamic label ("Encoding: UTF-16 LE").
  MaxL = MAX (MaxL, 20);
  *W   = (INTN)(MaxL + MaxA + 8) * FONT_W;
  *X   = MenuTitleX (Index);
  *Y   = TITLE_H + MENU_H;
  if (*X + *W > (INTN)gGfx.Width) {
    *X = (INTN)gGfx.Width - *W;
  }
}

INTN
MenuItemAt (
  UINTN  Index,
  INTN   X,
  INTN   Y
  )
{
  INTN        Px, Py, Pw, Ph, Iy;
  UINTN       I;
  CONST MENU  *M;

  MenuPopupRect (Index, &Px, &Py, &Pw, &Ph);
  if ((X < Px) || (X >= Px + Pw) || (Y < Py) || (Y >= Py + Ph)) {
    return -2;   // outside popup
  }

  M  = &gMenus[Index];
  Iy = Py + MENU_PAD;
  for (I = 0; I < M->Count; I++) {
    if (M->Items[I].Label == NULL) {
      Iy += MENU_SEP_H;
      continue;
    }

    if ((Y >= Iy) && (Y < Iy + MENU_ITEM_H)) {
      return MenuItemEnabled (&M->Items[I]) ? (INTN)I : -1;
    }

    Iy += MENU_ITEM_H;
  }

  return -1;
}

//
// ---------------- Layout ----------------
//
VOID
ViewLayout (
  VOID
  )
{
  INTN  Bottom;

  Bottom     = (INTN)gGfx.Height - (gEd.ShowStatus ? STATUS_H : 0);
  gEd.TextX  = 0;
  gEd.TextY  = TITLE_H + MENU_H;
  gEd.TextW  = (INTN)gGfx.Width - SCROLL_W;
  gEd.TextH  = Bottom - gEd.TextY - SCROLL_W;
  gEd.Rows   = (gEd.TextH > 4) ? (UINTN)(gEd.TextH - 4) / FONT_H : 1;
  gEd.Cols   = (gEd.TextW > TEXT_PAD) ? (UINTN)(gEd.TextW - TEXT_PAD) / FONT_W : 1;
  gEd.HasVScroll = TRUE;
  gEd.HasHScroll = TRUE;
}

UINTN
ViewMaxCols (
  VOID
  )
{
  UINTN  L, V;

  if (gEd.MaxColsDirty) {
    gEd.MaxCols = 0;
    for (L = 0; L < gEd.Doc.Count; L++) {
      V = EdVisCol (L, gEd.Doc.Lines[L].Len);
      if (V > gEd.MaxCols) {
        gEd.MaxCols = V;
      }
    }

    gEd.MaxColsDirty = FALSE;
  }

  return gEd.MaxCols;
}

STATIC
UINTN
MaxTop (
  VOID
  )
{
  return (gEd.Doc.Count > gEd.Rows) ? gEd.Doc.Count - gEd.Rows : 0;
}

STATIC
UINTN
MaxLeft (
  VOID
  )
{
  UINTN  M;

  M = ViewMaxCols () + 1;
  if (gEd.Cursor.Line < gEd.Doc.Count) {
    M = MAX (M, EdVisCol (gEd.Cursor.Line, gEd.Cursor.Col) + 1);
  }

  return (M > gEd.Cols) ? M - gEd.Cols : 0;
}

VOID
ViewScrollTo (
  INTN  Top
  )
{
  if (Top < 0) {
    Top = 0;
  }

  if ((UINTN)Top > MaxTop ()) {
    Top = (INTN)MaxTop ();
  }

  gEd.TopLine = (UINTN)Top;
}

VOID
ViewScrollLeftTo (
  INTN  Left
  )
{
  if (Left < 0) {
    Left = 0;
  }

  if ((UINTN)Left > MaxLeft ()) {
    Left = (INTN)MaxLeft ();
  }

  gEd.LeftCol = (UINTN)Left;
}

VOID
ViewVScrollGeom (
  INTN  *TrackY,
  INTN  *TrackH,
  INTN  *ThumbY,
  INTN  *ThumbH
  )
{
  UINTN  Total, Mt;

  *TrackY = gEd.TextY + SCROLL_W;
  *TrackH = gEd.TextH - 2 * SCROLL_W;
  Mt      = MaxTop ();
  Total   = Mt + gEd.Rows;
  *ThumbH = (Total == 0) ? *TrackH : (INTN)((UINTN)*TrackH * gEd.Rows / Total);
  *ThumbH = MAX (*ThumbH, 20);
  *ThumbH = MIN (*ThumbH, *TrackH);
  *ThumbY = *TrackY;
  if (Mt > 0) {
    *ThumbY += (INTN)((UINTN)(*TrackH - *ThumbH) * MIN (gEd.TopLine, Mt) / Mt);
  }
}

VOID
ViewHScrollGeom (
  INTN  *TrackX,
  INTN  *TrackW,
  INTN  *ThumbX,
  INTN  *ThumbW
  )
{
  UINTN  Total, Ml;

  *TrackX = gEd.TextX + SCROLL_W;
  *TrackW = gEd.TextW - 2 * SCROLL_W;
  Ml      = MaxLeft ();
  Total   = Ml + gEd.Cols;
  *ThumbW = (Total == 0) ? *TrackW : (INTN)((UINTN)*TrackW * gEd.Cols / Total);
  *ThumbW = MAX (*ThumbW, 20);
  *ThumbW = MIN (*ThumbW, *TrackW);
  *ThumbX = *TrackX;
  if (Ml > 0) {
    *ThumbX += (INTN)((UINTN)(*TrackW - *ThumbW) * MIN (gEd.LeftCol, Ml) / Ml);
  }
}

UINTN
ViewHitTest (
  INTN  X,
  INTN  Y
  )
{
  if (Y < TITLE_H) {
    return HIT_TITLE;
  }

  if (Y < TITLE_H + MENU_H) {
    return HIT_MENUBAR;
  }

  if ((Y >= gEd.TextY) && (Y < gEd.TextY + gEd.TextH)) {
    return (X < gEd.TextX + gEd.TextW) ? HIT_TEXT : HIT_VSCROLL;
  }

  if ((Y >= gEd.TextY + gEd.TextH) && (Y < gEd.TextY + gEd.TextH + SCROLL_W)) {
    return (X < gEd.TextX + gEd.TextW) ? HIT_HSCROLL : HIT_NONE;
  }

  return HIT_STATUS;
}

BOOLEAN
ViewMouseToPos (
  INTN  X,
  INTN  Y,
  POS   *P
  )
{
  INTN  Row, VCol;

  Row = (Y - gEd.TextY - 2);
  Row = (Row < 0) ? -1 : Row / FONT_H;
  Row += (INTN)gEd.TopLine;
  if (Row < 0) {
    Row = 0;
  }

  if (Row >= (INTN)gEd.Doc.Count) {
    Row = (INTN)gEd.Doc.Count - 1;
  }

  VCol = (X - gEd.TextX - TEXT_PAD + FONT_W / 2);
  VCol = (VCol < 0) ? 0 : VCol / FONT_W;
  VCol += (INTN)gEd.LeftCol;

  P->Line = (UINTN)Row;
  P->Col  = EdColFromVis (P->Line, (UINTN)VCol);
  return TRUE;
}

//
// ---------------- Rendering ----------------
//
STATIC
VOID
DrawTitle (
  VOID
  )
{
  CHAR16        Buf[MAX_PATH + 48];
  CONST CHAR16  *Name;

  Name = (gEd.Path[0] != 0) ? gEd.Path : L"Untitled";
  UnicodeSPrint (Buf, sizeof (Buf), L"%s%s - %s", gEd.Modified ? L"*" : L"", Name, NP_APPNAME);

  GfxFillRect (0, 0, (INTN)gGfx.Width, TITLE_H, C_TITLE_BG);
  //
  // Little page icon.
  //
  GfxFillRect (8, 5, 11, 14, 0xFFFFFF);
  GfxHLine (10, 9, 7, 0x7FA7D9);
  GfxHLine (10, 12, 7, 0x7FA7D9);
  GfxHLine (10, 15, 5, 0x7FA7D9);
  GfxDrawText (28, (TITLE_H - FONT_H) / 2, Buf, C_TITLE_FG);
}

STATIC
VOID
DrawMenuBar (
  VOID
  )
{
  UINTN    I;
  INTN     X, W, Y;
  BOOLEAN  Hover;

  Y = TITLE_H;
  GfxFillRect (0, Y, (INTN)gGfx.Width, MENU_H, C_WINDOW);
  GfxHLine (0, Y + MENU_H - 1, (INTN)gGfx.Width, 0xF0F0F0);

  for (I = 0; I < gMenuCount; I++) {
    X     = MenuTitleX (I);
    W     = MenuTitleW (I);
    Hover = (gInput.HasMouse && gInput.MouseY >= Y && gInput.MouseY < Y + MENU_H &&
             gInput.MouseX >= X && gInput.MouseX < X + W);
    if (gEd.OpenMenu == (INTN)I) {
      GfxFillRect (X, Y + 1, W, MENU_H - 2, C_MENU_HOVER);
      GfxFrameRect (X, Y + 1, W, MENU_H - 2, C_MENU_HOVER_B);
    } else if (Hover || (gEd.MenuKeyboard && gEd.MenuHot == (INTN)I && gEd.OpenMenu < 0)) {
      GfxFillRect (X, Y + 1, W, MENU_H - 2, 0xE5F3FF);
      GfxFrameRect (X, Y + 1, W, MENU_H - 2, 0xCCE8FF);
    }

    GfxDrawText (X + FONT_W, Y + (MENU_H - FONT_H) / 2, gMenus[I].Title, C_TEXT);
    if (gEd.MenuKeyboard) {
      //
      // Underline the mnemonic letter while the menu bar has keyboard focus.
      //
      UINTN  K;

      for (K = 0; gMenus[I].Title[K] != 0; K++) {
        CHAR16  C;

        C = gMenus[I].Title[K];
        if ((C | 0x20) == gMenus[I].Hotkey) {
          GfxHLine (X + FONT_W + (INTN)K * FONT_W, Y + (MENU_H - FONT_H) / 2 + 16, FONT_W, C_TEXT);
          break;
        }
      }
    }
  }
}

STATIC
VOID
DrawTextArea (
  VOID
  )
{
  UINTN    Row, LineIdx, I, V, Nv;
  INTN     X, Y, X0;
  LINE     *L;
  POS      S, E;
  BOOLEAN  Sel, InSel;
  UINTN    CursorV;

  GfxFillRect (gEd.TextX, gEd.TextY, gEd.TextW, gEd.TextH, C_WINDOW);
  GfxSetClip (gEd.TextX, gEd.TextY, gEd.TextX + gEd.TextW, gEd.TextY + gEd.TextH);

  Sel = EdHasSel ();
  EdSelRange (&S, &E);
  X0 = gEd.TextX + TEXT_PAD - (INTN)gEd.LeftCol * FONT_W;

  for (Row = 0; Row <= gEd.Rows; Row++) {
    LineIdx = gEd.TopLine + Row;
    if (LineIdx >= gEd.Doc.Count) {
      break;
    }

    L = &gEd.Doc.Lines[LineIdx];
    Y = gEd.TextY + 2 + (INTN)Row * FONT_H;
    V = 0;
    for (I = 0; I <= L->Len; I++) {
      POS  P;

      P.Line = LineIdx;
      P.Col  = I;
      InSel  = Sel && PosCmp (P, S) >= 0 && PosCmp (P, E) < 0;
      if (I == L->Len) {
        //
        // Show the selected line break as one highlighted cell.
        //
        if (InSel) {
          GfxFillRect (X0 + (INTN)V * FONT_W, Y, FONT_W, FONT_H, C_HILITE);
        }

        break;
      }

      Nv = (L->Text[I] == L'\t') ? (V / TAB_SIZE + 1) * TAB_SIZE : V + 1;
      X  = X0 + (INTN)V * FONT_W;
      if (X > gEd.TextX + gEd.TextW) {
        break;
      }

      if (X + (INTN)(Nv - V) * FONT_W >= gEd.TextX) {
        if (InSel) {
          GfxFillRect (X, Y, (INTN)(Nv - V) * FONT_W, FONT_H, C_HILITE);
        }

        if (L->Text[I] != L'\t') {
          GfxDrawChar (X, Y, L->Text[I], InSel ? C_HILITE_FG : C_TEXT);
        }
      }

      V = Nv;
    }
  }

  //
  // Caret.
  //
  if (gEd.CaretOn && (gEd.OpenMenu < 0) &&
      (gEd.Cursor.Line >= gEd.TopLine) && (gEd.Cursor.Line < gEd.TopLine + gEd.Rows + 1))
  {
    CursorV = EdVisCol (gEd.Cursor.Line, gEd.Cursor.Col);
    X       = X0 + (INTN)CursorV * FONT_W;
    Y       = gEd.TextY + 2 + (INTN)(gEd.Cursor.Line - gEd.TopLine) * FONT_H;
    GfxFillRect (X, Y, 2, FONT_H, 0x000000);
  }

  GfxResetClip ();
}

STATIC
VOID
DrawScrollButton (
  INTN   X,
  INTN   Y,
  UINTN  Dir
  )
{
  BOOLEAN  Hot;

  Hot = gInput.HasMouse && gInput.MouseX >= X && gInput.MouseX < X + SCROLL_W &&
        gInput.MouseY >= Y && gInput.MouseY < Y + SCROLL_W;
  GfxFillRect (X, Y, SCROLL_W, SCROLL_W, Hot ? C_FACE_DARK : C_FACE);
  GfxDrawArrow (X + SCROLL_W / 2, Y + SCROLL_W / 2, Dir, 0x606060);
}

STATIC
VOID
DrawScrollBars (
  VOID
  )
{
  INTN     Tx, Tw, Ty, Th, Hx, Hy;
  INTN     Sx, Sy;
  BOOLEAN  Hot;

  //
  // Vertical.
  //
  Sx = gEd.TextX + gEd.TextW;
  GfxFillRect (Sx, gEd.TextY, SCROLL_W, gEd.TextH, C_FACE);
  DrawScrollButton (Sx, gEd.TextY, 0);
  DrawScrollButton (Sx, gEd.TextY + gEd.TextH - SCROLL_W, 1);
  ViewVScrollGeom (&Ty, &Th, &Hy, &Hx);
  Hot = (gEd.Drag == DRAG_VTHUMB) ||
        (gInput.HasMouse && gInput.MouseX >= Sx && gInput.MouseX < Sx + SCROLL_W &&
         gInput.MouseY >= Hy && gInput.MouseY < Hy + Hx);
  GfxFillRect (Sx + 4, Hy + 1, SCROLL_W - 8, Hx - 2, Hot ? C_THUMB_HOT : C_THUMB);

  //
  // Horizontal.
  //
  Sy = gEd.TextY + gEd.TextH;
  GfxFillRect (gEd.TextX, Sy, gEd.TextW, SCROLL_W, C_FACE);
  DrawScrollButton (gEd.TextX, Sy, 2);
  DrawScrollButton (gEd.TextX + gEd.TextW - SCROLL_W, Sy, 3);
  ViewHScrollGeom (&Tx, &Tw, &Hx, &Hy);
  Hot = (gEd.Drag == DRAG_HTHUMB) ||
        (gInput.HasMouse && gInput.MouseY >= Sy && gInput.MouseY < Sy + SCROLL_W &&
         gInput.MouseX >= Hx && gInput.MouseX < Hx + Hy);
  GfxFillRect (Hx + 1, Sy + 4, Hy - 2, SCROLL_W - 8, Hot ? C_THUMB_HOT : C_THUMB);

  //
  // Corner.
  //
  GfxFillRect (Sx, Sy, SCROLL_W, SCROLL_W, C_FACE);
}

STATIC
VOID
DrawStatus (
  VOID
  )
{
  INTN    Y, X;
  CHAR16  Buf[64];

  if (!gEd.ShowStatus) {
    return;
  }

  Y = (INTN)gGfx.Height - STATUS_H;
  GfxFillRect (0, Y, (INTN)gGfx.Width, STATUS_H, C_FACE);
  GfxHLine (0, Y, (INTN)gGfx.Width, C_FACE_DARK);

  if (gEd.Message[0] != 0) {
    GfxDrawText (8, Y + 2, gEd.Message, C_TEXT);
  }

  X = (INTN)gGfx.Width - 14 * FONT_W;
  GfxVLine (X - 8, Y + 3, STATUS_H - 6, C_BORDER);
  GfxDrawText (X, Y + 2, EncName (gEd.Enc), C_TEXT);

  X -= 18 * FONT_W;
  GfxVLine (X - 8, Y + 3, STATUS_H - 6, C_BORDER);
  GfxDrawText (X, Y + 2, (gEd.Eol == EOL_CRLF) ? L"Windows (CRLF)" : L"Unix (LF)", C_TEXT);

  X -= 24 * FONT_W;
  GfxVLine (X - 8, Y + 3, STATUS_H - 6, C_BORDER);
  UnicodeSPrint (Buf, sizeof (Buf), L"Ln %d, Col %d", gEd.Cursor.Line + 1, gEd.Cursor.Col + 1);
  GfxDrawText (X, Y + 2, Buf, C_TEXT);
}

STATIC
VOID
DrawPopup (
  VOID
  )
{
  INTN             Px, Py, Pw, Ph, Iy;
  UINTN            I;
  CONST MENU       *M;
  CONST MENU_ITEM  *It;
  BOOLEAN          En;
  UINT32           Fg;
  INTN             AccelX;

  if (gEd.OpenMenu < 0) {
    return;
  }

  M = &gMenus[gEd.OpenMenu];
  MenuPopupRect ((UINTN)gEd.OpenMenu, &Px, &Py, &Pw, &Ph);
  GfxFillRect (Px + 3, Py + 3, Pw, Ph, 0xA0A0A0);   // shadow
  GfxFillRect (Px, Py, Pw, Ph, 0xF2F2F2);
  GfxFrameRect (Px, Py, Pw, Ph, 0xCCCCCC);

  Iy = Py + MENU_PAD;
  for (I = 0; I < M->Count; I++) {
    It = &M->Items[I];
    if (It->Label == NULL) {
      GfxHLine (Px + 4, Iy + MENU_SEP_H / 2, Pw - 8, 0xD7D7D7);
      Iy += MENU_SEP_H;
      continue;
    }

    En = MenuItemEnabled (It);
    if ((gEd.MenuHot == (INTN)I) && En) {
      GfxFillRect (Px + 2, Iy, Pw - 4, MENU_ITEM_H, 0x91C9F7);
    }

    Fg = En ? C_TEXT : C_GRAYTEXT;
    if ((It->Cmd == CMD_STATUSBAR) && gEd.ShowStatus) {
      INTN  K;

      for (K = 0; K < 3; K++) {
        GfxFillRect (Px + 7 + K, Iy + 10 + K, 2, 2, Fg);
      }

      for (K = 0; K < 5; K++) {
        GfxFillRect (Px + 10 + K, Iy + 12 - K, 2, 2, Fg);
      }
    }

    GfxDrawText (Px + 3 * FONT_W, Iy + (MENU_ITEM_H - FONT_H) / 2, MenuItemLabel (It), Fg);
    if ((It->Accel != NULL) && (It->Accel[0] != 0)) {
      AccelX = Px + Pw - (INTN)(StrLen (It->Accel) + 2) * FONT_W;
      GfxDrawText (AccelX, Iy + (MENU_ITEM_H - FONT_H) / 2, It->Accel, Fg);
    }

    Iy += MENU_ITEM_H;
  }
}

VOID
ViewRender (
  VOID
  )
{
  ViewLayout ();
  GfxResetClip ();
  DrawTitle ();
  DrawMenuBar ();
  DrawTextArea ();
  DrawScrollBars ();
  DrawStatus ();
  DrawPopup ();
}
