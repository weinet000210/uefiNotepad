/** @file
  Modal dialogs: message boxes, single-line input and Find/Replace.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

#define DLG_TITLE_H   26
#define DLG_BTN_W     96
#define DLG_BTN_H     26
#define DLG_MAX_FIELD 2
#define DLG_MAX_BTN   3
#define DLG_MAX_LINES 24


typedef struct {
  CONST CHAR16  *Title;
  CONST CHAR16  *Text;          // may contain '\n'
  DLG_FIELD     Fields[DLG_MAX_FIELD];
  UINTN         FieldCount;
  CONST CHAR16  *Buttons[DLG_MAX_BTN];
  UINTN         ButtonCount;
  UINTN         Focus;          // 0..FieldCount-1 fields, then buttons
  INTN          Pressed;        // button held down by mouse, -1 none
  // geometry
  INTN          X, Y, W, H;
  UINTN         TextLines;
  UINTN         LabelW;         // label column width in chars
} DLG;

STATIC
UINTN
CountLines (
  CONST CHAR16  *S,
  UINTN         *MaxW
  )
{
  UINTN  Lines, W;

  Lines = 0;
  *MaxW = 0;
  if ((S == NULL) || (*S == 0)) {
    return 0;
  }

  Lines = 1;
  W     = 0;
  for ( ; *S != 0; S++) {
    if (*S == L'\n') {
      Lines++;
      W = 0;
    } else {
      W++;
      *MaxW = MAX (*MaxW, W);
    }
  }

  return Lines;
}

STATIC
VOID
DlgLayout (
  DLG  *D
  )
{
  UINTN  MaxW, I, BodyH;

  D->TextLines = MIN (CountLines (D->Text, &MaxW), DLG_MAX_LINES);
  D->LabelW    = 0;
  for (I = 0; I < D->FieldCount; I++) {
    D->LabelW = MAX (D->LabelW, StrLen (D->Fields[I].Label) + 1);
  }

  D->W = (INTN)MAX (MaxW * FONT_W + 48, 420);
  if (D->FieldCount > 0) {
    D->W = MAX (D->W, 480);
  }

  D->W  = MIN (D->W, (INTN)gGfx.Width - 40);
  BodyH = 16 + D->TextLines * FONT_H + D->FieldCount * (DLG_FIELD_H + 10) + 8;
  D->H  = DLG_TITLE_H + (INTN)BodyH + DLG_BTN_H + 22;
  D->X  = ((INTN)gGfx.Width - D->W) / 2;
  D->Y  = ((INTN)gGfx.Height - D->H) / 2;
}

STATIC
VOID
FieldRect (
  DLG    *D,
  UINTN  I,
  INTN   *X,
  INTN   *Y,
  INTN   *W
  )
{
  *X = D->X + 20 + (INTN)D->LabelW * FONT_W;
  *Y = D->Y + DLG_TITLE_H + 16 + (INTN)D->TextLines * FONT_H + (INTN)I * (DLG_FIELD_H + 10);
  *W = D->X + D->W - 20 - *X;
}

STATIC
VOID
ButtonRect (
  DLG    *D,
  UINTN  I,
  INTN   *X,
  INTN   *Y
  )
{
  INTN  Right;

  Right = D->X + D->W - 14;
  *X    = Right - (INTN)(D->ButtonCount - I) * (DLG_BTN_W + 8) + 8;
  *Y    = D->Y + D->H - DLG_BTN_H - 11;
}

VOID
DlgFieldDraw (
  DLG_FIELD  *F,
  INTN       Fx,
  INTN       Fy,
  INTN       Fw,
  BOOLEAN    Focused
  )
{
  UINTN  Visible;
  INTN   Cx;

  GfxFillRect (Fx, Fy, Fw, DLG_FIELD_H, C_WINDOW);
  GfxFrameRect (Fx, Fy, Fw, DLG_FIELD_H, Focused ? C_HILITE : 0x7A7A7A);

  Visible = (UINTN)(Fw - 10) / FONT_W;
  if (F->Cursor < F->Scroll) {
    F->Scroll = F->Cursor;
  } else if (F->Cursor > F->Scroll + Visible) {
    F->Scroll = F->Cursor - Visible;
  }

  GfxSetClip (Fx + 2, Fy + 2, Fx + Fw - 2, Fy + DLG_FIELD_H - 2);
  if (F->AllSel && Focused && (F->Buf[0] != 0)) {
    GfxFillRect (Fx + 5, Fy + 3, (INTN)(StrLen (F->Buf) - F->Scroll) * FONT_W, DLG_FIELD_H - 6, C_HILITE);
    GfxDrawText (Fx + 5, Fy + (DLG_FIELD_H - FONT_H) / 2, F->Buf + F->Scroll, C_HILITE_FG);
  } else {
    GfxDrawText (Fx + 5, Fy + (DLG_FIELD_H - FONT_H) / 2, F->Buf + F->Scroll, C_TEXT);
  }

  if (Focused && gEd.CaretOn) {
    Cx = Fx + 5 + (INTN)(F->Cursor - F->Scroll) * FONT_W;
    GfxFillRect (Cx, Fy + 3, 1, DLG_FIELD_H - 6, 0x000000);
  }

  GfxResetClip ();
}

STATIC
VOID
DlgDraw (
  DLG  *D
  )
{
  UINTN         I, Line;
  INTN          Fx, Fy, Fw, Bx, By, Ty;
  CONST CHAR16  *S;
  CONST CHAR16  *Start;
  BOOLEAN       Focused;
  DLG_FIELD     *F;

  GfxFillRect (D->X + 4, D->Y + 4, D->W, D->H, 0x808080);
  GfxFillRect (D->X, D->Y, D->W, D->H, C_WINDOW);
  GfxFrameRect (D->X, D->Y, D->W, D->H, C_HILITE);
  GfxDrawText (D->X + 12, D->Y + (DLG_TITLE_H - FONT_H) / 2 + 1, D->Title, C_TEXT);

  //
  // Message text.
  //
  Ty    = D->Y + DLG_TITLE_H + 12;
  S     = D->Text;
  Line  = 0;
  if (S != NULL) {
    Start = S;
    for ( ; Line < D->TextLines; S++) {
      if ((*S == L'\n') || (*S == 0)) {
        GfxSetClip (D->X + 1, D->Y, D->X + D->W - 16, D->Y + D->H);
        GfxDrawTextN (D->X + 20, Ty + (INTN)Line * FONT_H, Start, (UINTN)(S - Start), C_TEXT);
        GfxResetClip ();
        Line++;
        if (*S == 0) {
          break;
        }

        Start = S + 1;
      }
    }
  }

  //
  // Fields.
  //
  for (I = 0; I < D->FieldCount; I++) {
    F       = &D->Fields[I];
    Focused = (D->Focus == I);
    FieldRect (D, I, &Fx, &Fy, &Fw);
    GfxDrawText (D->X + 20, Fy + (DLG_FIELD_H - FONT_H) / 2, F->Label, C_TEXT);
    DlgFieldDraw (F, Fx, Fy, Fw, Focused);
  }

  //
  // Button strip.
  //
  GfxFillRect (D->X + 1, D->Y + D->H - DLG_BTN_H - 22, D->W - 2, DLG_BTN_H + 21, C_FACE);
  for (I = 0; I < D->ButtonCount; I++) {
    BOOLEAN  Hot;

    ButtonRect (D, I, &Bx, &By);
    Focused = (D->Focus == D->FieldCount + I) || ((D->Focus < D->FieldCount) && (I == 0));
    Hot     = gInput.HasMouse && gInput.MouseX >= Bx && gInput.MouseX < Bx + DLG_BTN_W &&
              gInput.MouseY >= By && gInput.MouseY < By + DLG_BTN_H;
    GfxFillRect (Bx, By, DLG_BTN_W, DLG_BTN_H, (D->Pressed == (INTN)I) ? 0xCCE4F7 : (Hot ? 0xE5F1FB : C_BTN));
    GfxFrameRect (Bx, By, DLG_BTN_W, DLG_BTN_H, (Focused || Hot) ? C_HILITE : C_BORDER);
    if (D->Focus == D->FieldCount + I) {
      GfxFrameRect (Bx + 1, By + 1, DLG_BTN_W - 2, DLG_BTN_H - 2, C_HILITE);
    }

    GfxDrawText (
      Bx + (DLG_BTN_W - (INTN)StrLen (D->Buttons[I]) * FONT_W) / 2,
      By + (DLG_BTN_H - FONT_H) / 2,
      D->Buttons[I],
      C_TEXT
      );
  }
}

STATIC
VOID
DlgPresent (
  DLG  *D
  )
{
  ViewRender ();
  DlgDraw (D);
  GfxPresent ();
}

BOOLEAN
DlgFieldKey (
  DLG_FIELD    *F,
  INPUT_EVENT  *Ev
  )
{
  UINTN    Len;
  BOOLEAN  WasAll;

  WasAll    = F->AllSel;
  F->AllSel = FALSE;
  if (WasAll && (Ev->Scan == SCAN_NULL) && !Ev->Ctrl &&
      ((Ev->Char == CHAR_BACKSPACE) || (Ev->Char >= 0x20)))
  {
    F->Buf[0] = 0;
    F->Cursor = 0;
    F->Scroll = 0;
    if (Ev->Char == CHAR_BACKSPACE) {
      return TRUE;
    }
  }

  if (WasAll && (Ev->Scan == SCAN_DELETE)) {
    F->Buf[0] = 0;
    F->Cursor = 0;
    return TRUE;
  }

  Len = StrLen (F->Buf);
  switch (Ev->Scan) {
    case SCAN_LEFT:
      if (F->Cursor > 0) {
        F->Cursor--;
      }

      return TRUE;
    case SCAN_RIGHT:
      if (F->Cursor < Len) {
        F->Cursor++;
      }

      return TRUE;
    case SCAN_HOME:
      F->Cursor = 0;
      return TRUE;
    case SCAN_END:
      F->Cursor = Len;
      return TRUE;
    case SCAN_DELETE:
      if (F->Cursor < Len) {
        CopyMem (F->Buf + F->Cursor, F->Buf + F->Cursor + 1, (Len - F->Cursor) * sizeof (CHAR16));
      }

      return TRUE;
    default:
      break;
  }

  if (Ev->Ctrl) {
    if ((Ev->Char == L'v') && (gEd.Clip != NULL)) {
      UINTN  I;

      if (WasAll) {
        F->Buf[0] = 0;
        F->Cursor = 0;
        Len       = 0;
      }

      for (I = 0; I < gEd.ClipLen && gEd.Clip[I] != L'\n' && Len + 1 < F->BufLen; I++) {
        CopyMem (F->Buf + F->Cursor + 1, F->Buf + F->Cursor, (Len - F->Cursor + 1) * sizeof (CHAR16));
        F->Buf[F->Cursor++] = gEd.Clip[I];
        Len++;
      }

      return TRUE;
    }

    if (Ev->Char == L'a') {
      F->Cursor = Len;
      F->AllSel = TRUE;
      return TRUE;
    }

    return FALSE;
  }

  if (Ev->Char == CHAR_BACKSPACE) {
    if (F->Cursor > 0) {
      CopyMem (F->Buf + F->Cursor - 1, F->Buf + F->Cursor, (Len - F->Cursor + 1) * sizeof (CHAR16));
      F->Cursor--;
    }

    return TRUE;
  }

  if ((Ev->Char >= 0x20) && (Len + 1 < F->BufLen)) {
    if (F->Numeric && ((Ev->Char < L'0') || (Ev->Char > L'9'))) {
      return TRUE;
    }

    CopyMem (F->Buf + F->Cursor + 1, F->Buf + F->Cursor, (Len - F->Cursor + 1) * sizeof (CHAR16));
    F->Buf[F->Cursor++] = Ev->Char;
    return TRUE;
  }

  return FALSE;
}

/**
  Run a dialog. Returns the 1-based index of the button chosen, 0 on Esc.
**/
STATIC
INTN
DlgRun (
  DLG  *D
  )
{
  INPUT_EVENT  Ev;
  UINTN        Total, I;
  INTN         Fx, Fy, Fw, Bx, By;
  INTN         SavedMenu;
  UINTN        LastBlink;

  SavedMenu    = gEd.OpenMenu;
  gEd.OpenMenu = -1;
  D->Pressed   = -1;
  gModal++;
  Total        = D->FieldCount + D->ButtonCount;
  for (I = 0; I < D->FieldCount; I++) {
    D->Fields[I].Cursor = StrLen (D->Fields[I].Buf);
    D->Fields[I].Scroll = 0;
    D->Fields[I].AllSel = (D->Fields[I].Buf[0] != 0);
  }

  DlgLayout (D);
  gEd.CaretOn  = TRUE;
  LastBlink    = (UINTN)gInput.Ticks;
  DlgPresent (D);

  for ( ; ;) {
    UpdateMouseCursor ();
    InputWait (&Ev);

    if (Ev.Type == EV_TIMER) {
      if ((UINTN)gInput.Ticks - LastBlink >= 26) {
        LastBlink   = (UINTN)gInput.Ticks;
        gEd.CaretOn = !gEd.CaretOn;
        DlgPresent (D);
      }

      continue;
    }

    if (Ev.Type == EV_KEY) {
      gEd.CaretOn = TRUE;
      LastBlink   = (UINTN)gInput.Ticks;
      if (Ev.Scan == SCAN_ESC) {
        gEd.OpenMenu = SavedMenu;
        return 0;
      }

      if ((Ev.Char == CHAR_CARRIAGE_RETURN) || (Ev.Char == CHAR_LINEFEED)) {
        gEd.OpenMenu = SavedMenu;
        if (D->Focus >= D->FieldCount) {
          return (INTN)(D->Focus - D->FieldCount) + 1;
        }

        return 1;
      }

      if ((Ev.Char == CHAR_TAB) || (Ev.Scan == SCAN_DOWN) || (Ev.Scan == SCAN_UP)) {
        BOOLEAN  Back;

        Back     = (Ev.Scan == SCAN_UP) || ((Ev.Char == CHAR_TAB) && Ev.Shift);
        D->Focus = Back ? (D->Focus + Total - 1) % Total : (D->Focus + 1) % Total;
        DlgPresent (D);
        continue;
      }

      if (D->Focus < D->FieldCount) {
        DlgFieldKey (&D->Fields[D->Focus], &Ev);
      } else if (Ev.Scan == SCAN_LEFT) {
        if (D->Focus > D->FieldCount) {
          D->Focus--;
        }
      } else if (Ev.Scan == SCAN_RIGHT) {
        if (D->Focus + 1 < Total) {
          D->Focus++;
        }
      } else if (Ev.Char == L' ') {
        gEd.OpenMenu = SavedMenu;
        return (INTN)(D->Focus - D->FieldCount) + 1;
      }

      DlgPresent (D);
      continue;
    }

    if (Ev.Type == EV_MOUSE) {
      if (Ev.Pressed & MB_LEFT) {
        for (I = 0; I < D->ButtonCount; I++) {
          ButtonRect (D, I, &Bx, &By);
          if ((Ev.X >= Bx) && (Ev.X < Bx + DLG_BTN_W) && (Ev.Y >= By) && (Ev.Y < By + DLG_BTN_H)) {
            D->Pressed = (INTN)I;
          }
        }

        for (I = 0; I < D->FieldCount; I++) {
          FieldRect (D, I, &Fx, &Fy, &Fw);
          if ((Ev.X >= Fx) && (Ev.X < Fx + Fw) && (Ev.Y >= Fy) && (Ev.Y < Fy + DLG_FIELD_H)) {
            INTN   C;
            UINTN  Len;

            D->Focus = I;
            D->Fields[I].AllSel = FALSE;
            Len      = StrLen (D->Fields[I].Buf);
            C        = (Ev.X - Fx - 5 + FONT_W / 2) / FONT_W + (INTN)D->Fields[I].Scroll;
            D->Fields[I].Cursor = (C < 0) ? 0 : MIN ((UINTN)C, Len);
          }
        }
      }

      if ((Ev.Released & MB_LEFT) && (D->Pressed >= 0)) {
        ButtonRect (D, (UINTN)D->Pressed, &Bx, &By);
        if ((Ev.X >= Bx) && (Ev.X < Bx + DLG_BTN_W) && (Ev.Y >= By) && (Ev.Y < By + DLG_BTN_H)) {
          gEd.OpenMenu = SavedMenu;
          return D->Pressed + 1;
        }

        D->Pressed = -1;
      }

      DlgPresent (D);
    }
  }
}

INTN
DlgMessage (
  CONST CHAR16  *Title,
  CONST CHAR16  *Text,
  CONST CHAR16  *B1,
  CONST CHAR16  *B2,
  CONST CHAR16  *B3
  )
{
  DLG   D;
  INTN  R;

  ZeroMem (&D, sizeof (D));
  D.Title = Title;
  D.Text  = Text;
  D.Buttons[D.ButtonCount++] = B1;
  if (B2 != NULL) {
    D.Buttons[D.ButtonCount++] = B2;
  }

  if (B3 != NULL) {
    D.Buttons[D.ButtonCount++] = B3;
  }

  R = DlgRun (&D);
  gModal--;
  FullRedraw ();
  return R;
}

BOOLEAN
DlgInput (
  CONST CHAR16  *Title,
  CONST CHAR16  *Prompt,
  CHAR16        *Buf,
  UINTN         BufLen
  )
{
  DLG     D;
  INTN    R;
  CHAR16  *Tmp;

  Tmp = AllocateCopyPool (BufLen * sizeof (CHAR16), Buf);
  if (Tmp == NULL) {
    return FALSE;
  }

  ZeroMem (&D, sizeof (D));
  D.Title                = Title;
  D.Fields[0].Label      = Prompt;
  D.Fields[0].Buf        = Tmp;
  D.Fields[0].BufLen     = BufLen;
  D.Fields[0].Numeric    = (StrCmp (Title, L"Go To Line") == 0);
  D.FieldCount           = 1;
  D.Buttons[0]           = L"OK";
  D.Buttons[1]           = L"Cancel";
  D.ButtonCount          = 2;
  R                      = DlgRun (&D);
  gModal--;
  if (R == 1) {
    StrCpyS (Buf, BufLen, Tmp);
  }

  FreePool (Tmp);
  FullRedraw ();
  return R == 1;
}

INTN
DlgReplace (
  CHAR16  *Find,
  CHAR16  *Repl,
  UINTN   BufLen
  )
{
  DLG   D;
  INTN  R;

  ZeroMem (&D, sizeof (D));
  D.Title             = L"Replace";
  D.Fields[0].Label   = L"Find what:";
  D.Fields[0].Buf     = Find;
  D.Fields[0].BufLen  = BufLen;
  D.Fields[1].Label   = L"Replace with:";
  D.Fields[1].Buf     = Repl;
  D.Fields[1].BufLen  = BufLen;
  D.FieldCount        = 2;
  D.Buttons[0]        = L"Replace";
  D.Buttons[1]        = L"Replace All";
  D.Buttons[2]        = L"Cancel";
  D.ButtonCount       = 3;
  R                   = DlgRun (&D);
  gModal--;
  FullRedraw ();
  return (R == 3) ? 0 : R;
}
