/** @file
  Keyboard and mouse input. Keys come from SimpleTextInputEx (falls back to
  SimpleTextIn); the mouse comes from AbsolutePointer (tablet / touch) or
  SimplePointer (relative mouse), polled on a 20 ms timer.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

INPUT  gInput;

#define TICK_100NS       (20 * 10000)  // 20 ms
#define DBLCLICK_TICKS   25            // 500 ms

STATIC INTN  mFracX, mFracY;
STATIC UINT32  mAbsButtons;
STATIC UINT32  mPtrButtons;

EFI_STATUS
InputInit (
  VOID
  )
{
  EFI_STATUS  Status;

  ZeroMem (&gInput, sizeof (gInput));

  Status = gBS->HandleProtocol (gST->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID **)&gInput.InEx);
  if (EFI_ERROR (Status)) {
    gInput.InEx = NULL;
  }

  //
  // ConSplitter publishes both pointer protocols on the console-in handle,
  // merging every physical device. Fall back to any instance in the system.
  //
  Status = gBS->HandleProtocol (gST->ConsoleInHandle, &gEfiAbsolutePointerProtocolGuid, (VOID **)&gInput.Abs);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiAbsolutePointerProtocolGuid, NULL, (VOID **)&gInput.Abs);
    if (EFI_ERROR (Status)) {
      gInput.Abs = NULL;
    }
  }

  Status = gBS->HandleProtocol (gST->ConsoleInHandle, &gEfiSimplePointerProtocolGuid, (VOID **)&gInput.Ptr);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiSimplePointerProtocolGuid, NULL, (VOID **)&gInput.Ptr);
    if (EFI_ERROR (Status)) {
      gInput.Ptr = NULL;
    }
  }

  if (gInput.Abs != NULL) {
    gInput.Abs->Reset (gInput.Abs, FALSE);
  }

  if (gInput.Ptr != NULL) {
    gInput.Ptr->Reset (gInput.Ptr, FALSE);
  }

  gInput.MouseX = (INTN)gGfx.Width / 2;
  gInput.MouseY = (INTN)gGfx.Height / 2;

  Status = gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &gInput.Timer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return gBS->SetTimer (gInput.Timer, TimerPeriodic, TICK_100NS);
}

VOID
InputShutdown (
  VOID
  )
{
  if (gInput.Timer != NULL) {
    gBS->SetTimer (gInput.Timer, TimerCancel, 0);
    gBS->CloseEvent (gInput.Timer);
    gInput.Timer = NULL;
  }
}

STATIC
BOOLEAN
ReadKey (
  INPUT_EVENT  *Ev
  )
{
  EFI_KEY_DATA   Kd;
  EFI_INPUT_KEY  K;
  EFI_STATUS     Status;
  UINT32         Sh;
  BOOLEAN        ShValid;

  ZeroMem (&Kd, sizeof (Kd));
  if (gInput.InEx != NULL) {
    Status = gInput.InEx->ReadKeyStrokeEx (gInput.InEx, &Kd);
  } else {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &K);
    if (!EFI_ERROR (Status)) {
      Kd.Key = K;
    }
  }

  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Sh      = Kd.KeyState.KeyShiftState;
  ShValid = (Sh & EFI_SHIFT_STATE_VALID) != 0;

  //
  // A pure modifier press (partial key) carries neither scan nor char.
  //
  if ((Kd.Key.ScanCode == SCAN_NULL) && (Kd.Key.UnicodeChar == 0)) {
    if (ShValid) {
      gInput.ShiftHeld = (Sh & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0;
    }

    return FALSE;
  }

  ZeroMem (Ev, sizeof (*Ev));
  Ev->Type = EV_KEY;
  Ev->Scan = Kd.Key.ScanCode;
  Ev->Char = Kd.Key.UnicodeChar;
  if (ShValid) {
    Ev->Ctrl  = (Sh & (EFI_LEFT_CONTROL_PRESSED | EFI_RIGHT_CONTROL_PRESSED)) != 0;
    Ev->Shift = (Sh & (EFI_LEFT_SHIFT_PRESSED | EFI_RIGHT_SHIFT_PRESSED)) != 0;
    Ev->Alt   = (Sh & (EFI_LEFT_ALT_PRESSED | EFI_RIGHT_ALT_PRESSED)) != 0;
    gInput.ShiftHeld = Ev->Shift;
  }

  //
  // Normalize Ctrl+letter: some consoles deliver 0x01..0x1A, others the
  // letter itself with the Ctrl shift-state bit.
  //
  if ((Ev->Char >= 1) && (Ev->Char <= 26)) {
    if (Ev->Ctrl ||
        ((Ev->Char != CHAR_BACKSPACE) && (Ev->Char != CHAR_TAB) &&
         (Ev->Char != CHAR_LINEFEED) && (Ev->Char != CHAR_CARRIAGE_RETURN)))
    {
      Ev->Char = (CHAR16)(L'a' + Ev->Char - 1);
      Ev->Ctrl = TRUE;
    }
  }

  if (Ev->Ctrl && (Ev->Char >= L'A') && (Ev->Char <= L'Z')) {
    Ev->Char = (CHAR16)(Ev->Char - L'A' + L'a');
  }

  if (Ev->Scan == SCAN_ESC) {
    Ev->Char = 0;
  }

  return TRUE;
}

STATIC
BOOLEAN
PollMouse (
  INPUT_EVENT  *Ev
  )
{
  EFI_ABSOLUTE_POINTER_STATE  As;
  EFI_SIMPLE_POINTER_STATE    Ss;
  EFI_STATUS                  Status;
  INTN                        NewX, NewY, Wheel;
  UINT32                      NewB;
  BOOLEAN                     Got;
  UINT64                      Range;
  INTN                        Res;

  NewX  = gInput.MouseX;
  NewY  = gInput.MouseY;
  NewB  = gInput.Buttons;
  Wheel = 0;
  Got   = FALSE;

  if (gInput.Abs != NULL) {
    Status = gInput.Abs->GetState (gInput.Abs, &As);
    if (!EFI_ERROR (Status)) {
      EFI_ABSOLUTE_POINTER_MODE  *M;

      M        = gInput.Abs->Mode;
      Got      = TRUE;
      Range    = M->AbsoluteMaxX - M->AbsoluteMinX;
      if (Range > 0) {
        NewX = (INTN)DivU64x64Remainder (MultU64x64 (As.CurrentX - M->AbsoluteMinX, gGfx.Width - 1), Range, NULL);
      } else {
        NewX = (INTN)As.CurrentX;
      }

      Range = M->AbsoluteMaxY - M->AbsoluteMinY;
      if (Range > 0) {
        NewY = (INTN)DivU64x64Remainder (MultU64x64 (As.CurrentY - M->AbsoluteMinY, gGfx.Height - 1), Range, NULL);
      } else {
        NewY = (INTN)As.CurrentY;
      }

      mAbsButtons = 0;
      if (As.ActiveButtons & EFI_ABSP_TouchActive) {
        mAbsButtons |= MB_LEFT;
      }

      if (As.ActiveButtons & EFI_ABS_AltActive) {
        mAbsButtons |= MB_RIGHT;
      }
    }
  }

  if (gInput.Ptr != NULL) {
    Status = gInput.Ptr->GetState (gInput.Ptr, &Ss);
    if (!EFI_ERROR (Status)) {
      Got = TRUE;
      Res = (INTN)gInput.Ptr->Mode->ResolutionX;
      if (Res <= 0) {
        Res = 1;
      }

      mFracX += Ss.RelativeMovementX * 8;
      NewX   += mFracX / Res;
      mFracX  = mFracX % Res;
      Res     = (INTN)gInput.Ptr->Mode->ResolutionY;
      if (Res <= 0) {
        Res = 1;
      }

      mFracY += Ss.RelativeMovementY * 8;
      NewY   += mFracY / Res;
      mFracY  = mFracY % Res;
      if (Ss.RelativeMovementZ != 0) {
        Wheel = (Ss.RelativeMovementZ > 0) ? -1 : 1;
      }

      mPtrButtons = (Ss.LeftButton ? MB_LEFT : 0) | (Ss.RightButton ? MB_RIGHT : 0);
    }
  }

  if (!Got) {
    return FALSE;
  }

  NewB = mAbsButtons | mPtrButtons;

  gInput.HasMouse = TRUE;
  NewX            = MAX (0, MIN (NewX, (INTN)gGfx.Width - 1));
  NewY            = MAX (0, MIN (NewY, (INTN)gGfx.Height - 1));

  if ((NewX == gInput.MouseX) && (NewY == gInput.MouseY) && (NewB == gInput.Buttons) && (Wheel == 0)) {
    return FALSE;
  }

  ZeroMem (Ev, sizeof (*Ev));
  Ev->Type     = EV_MOUSE;
  Ev->X        = NewX;
  Ev->Y        = NewY;
  Ev->Buttons  = NewB;
  Ev->Pressed  = NewB & ~gInput.Buttons;
  Ev->Released = gInput.Buttons & ~NewB;
  Ev->Wheel    = Wheel;
  Ev->Shift    = gInput.ShiftHeld;

  if (Ev->Pressed & MB_LEFT) {
    if ((gInput.Ticks - gInput.LastClickTick <= DBLCLICK_TICKS) &&
        (ABS (NewX - gInput.LastClickX) <= 4) && (ABS (NewY - gInput.LastClickY) <= 4))
    {
      Ev->DoubleClick       = TRUE;
      gInput.LastClickTick = 0;
    } else {
      gInput.LastClickTick = gInput.Ticks;
    }

    gInput.LastClickX = NewX;
    gInput.LastClickY = NewY;
  }

  gInput.MouseX  = NewX;
  gInput.MouseY  = NewY;
  gInput.Buttons = NewB;
  return TRUE;
}

VOID
InputWait (
  INPUT_EVENT  *Ev
  )
{
  EFI_EVENT  Events[2];
  UINTN      Count;
  UINTN      Index;

  for ( ; ;) {
    if (ReadKey (Ev)) {
      return;
    }

    if (PollMouse (Ev)) {
      return;
    }

    Count           = 0;
    Events[Count++] = gInput.Timer;
    Events[Count++] = (gInput.InEx != NULL) ? gInput.InEx->WaitForKeyEx : gST->ConIn->WaitForKey;

    if (EFI_ERROR (gBS->WaitForEvent (Count, Events, &Index))) {
      continue;
    }

    if (Index == 0) {
      gInput.Ticks++;
      ZeroMem (Ev, sizeof (*Ev));
      Ev->Type = EV_TIMER;
      //
      // Give the mouse a chance first so motion is not delayed a tick.
      //
      if (PollMouse (Ev)) {
        return;
      }

      ZeroMem (Ev, sizeof (*Ev));
      Ev->Type = EV_TIMER;
      return;
    }
  }
}
