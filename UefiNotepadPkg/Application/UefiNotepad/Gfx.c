/** @file
  GOP back-buffer drawing, bitmap text and the software mouse cursor.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

extern EFI_NARROW_GLYPH  gNpGlyphs[];
extern UINTN             gNpGlyphCount;

GFX  gGfx;

STATIC CONST UINT8  *mGlyphLo[256];
STATIC CONST UINT8  *mGlyphBad;

//
// Mouse cursor state.
//
STATIC BOOLEAN  mMouseShown;
STATIC INTN     mMouseX, mMouseY;
STATIC BOOLEAN  mMouseIBeam;

#define CUR_W  12
#define CUR_H  19

// Arrow: '#' black outline, '.' white fill.
STATIC CONST CHAR8  *mArrow[CUR_H] = {
  "#           ",
  "##          ",
  "#.#         ",
  "#..#        ",
  "#...#       ",
  "#....#      ",
  "#.....#     ",
  "#......#    ",
  "#.......#   ",
  "#........#  ",
  "#.........# ",
  "#......#####",
  "#...#..#    ",
  "#..# #..#   ",
  "#.#  #..#   ",
  "##    #..#  ",
  "#     #..#  ",
  "       ##   ",
  "            "
};

// I-beam with hot spot in the middle.
STATIC CONST CHAR8  *mIBeam[CUR_H] = {
  "  ###.###   ",
  "  #..#..#   ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "    #.#     ",
  "  #..#..#   ",
  "  ###.###   ",
  "            "
};

STATIC
EFI_GRAPHICS_OUTPUT_BLT_PIXEL
Px (
  UINT32  Rgb
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  P;

  P.Blue     = (UINT8)(Rgb & 0xFF);
  P.Green    = (UINT8)((Rgb >> 8) & 0xFF);
  P.Red      = (UINT8)((Rgb >> 16) & 0xFF);
  P.Reserved = 0;
  return P;
}

STATIC
VOID
BuildGlyphTable (
  VOID
  )
{
  UINTN  Index;

  for (Index = 0; Index < gNpGlyphCount; Index++) {
    if (gNpGlyphs[Index].UnicodeWeight == 0) {
      continue;
    }

    if (gNpGlyphs[Index].UnicodeWeight < 256) {
      mGlyphLo[gNpGlyphs[Index].UnicodeWeight] = gNpGlyphs[Index].GlyphCol1;
    }

    if (gNpGlyphs[Index].UnicodeWeight == BLOCKELEMENT_LIGHT_SHADE) {
      mGlyphBad = gNpGlyphs[Index].GlyphCol1;
    }
  }

  if (mGlyphBad == NULL) {
    mGlyphBad = mGlyphLo['?'];
  }
}

STATIC
CONST UINT8 *
FindGlyph (
  CHAR16  Ch
  )
{
  UINTN  Index;

  if ((Ch < 256) && (mGlyphLo[Ch] != NULL)) {
    return mGlyphLo[Ch];
  }

  for (Index = 0; Index < gNpGlyphCount; Index++) {
    if (gNpGlyphs[Index].UnicodeWeight == Ch) {
      return gNpGlyphs[Index].GlyphCol1;
    }
  }

  return mGlyphBad;
}

STATIC
BOOLEAN
PickMode (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINTN                                 Size;
  UINT32                                Mode;
  UINT32                                Best;
  UINTN                                 BestArea;
  EFI_STATUS                            Status;

  Gop = gGfx.Gop;
  if ((Gop->Mode->Info->HorizontalResolution >= 800) &&
      (Gop->Mode->Info->VerticalResolution >= 600))
  {
    return FALSE;
  }

  Best     = Gop->Mode->Mode;
  BestArea = 0;
  for (Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
    Status = Gop->QueryMode (Gop, Mode, &Size, &Info);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if ((Info->HorizontalResolution >= 800) && (Info->VerticalResolution >= 600) &&
        (Info->HorizontalResolution <= 1920) && (Info->VerticalResolution <= 1200))
    {
      if ((UINTN)Info->HorizontalResolution * Info->VerticalResolution > BestArea) {
        BestArea = (UINTN)Info->HorizontalResolution * Info->VerticalResolution;
        Best     = Mode;
      }
    }

    FreePool (Info);
  }

  if ((BestArea != 0) && (Best != Gop->Mode->Mode)) {
    if (!EFI_ERROR (Gop->SetMode (Gop, Best))) {
      return TRUE;
    }
  }

  return FALSE;
}

EFI_STATUS
GfxInit (
  VOID
  )
{
  EFI_STATUS  Status;

  ZeroMem (&gGfx, sizeof (gGfx));
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&gGfx.Gop);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gGfx.Gop);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  gGfx.OrigMode    = gGfx.Gop->Mode->Mode;
  gGfx.ModeChanged = PickMode ();
  gGfx.Width       = gGfx.Gop->Mode->Info->HorizontalResolution;
  gGfx.Height      = gGfx.Gop->Mode->Info->VerticalResolution;
  gGfx.Back        = AllocateZeroPool (gGfx.Width * gGfx.Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (gGfx.Back == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  BuildGlyphTable ();
  GfxResetClip ();
  return EFI_SUCCESS;
}

VOID
GfxShutdown (
  VOID
  )
{
  if (gGfx.Back != NULL) {
    FreePool (gGfx.Back);
    gGfx.Back = NULL;
  }

  if (gGfx.ModeChanged) {
    gGfx.Gop->SetMode (gGfx.Gop, gGfx.OrigMode);
  }
}

VOID
GfxSetClip (
  INTN  X0,
  INTN  Y0,
  INTN  X1,
  INTN  Y1
  )
{
  gGfx.ClipX0 = MAX (X0, 0);
  gGfx.ClipY0 = MAX (Y0, 0);
  gGfx.ClipX1 = MIN (X1, (INTN)gGfx.Width);
  gGfx.ClipY1 = MIN (Y1, (INTN)gGfx.Height);
}

VOID
GfxResetClip (
  VOID
  )
{
  GfxSetClip (0, 0, (INTN)gGfx.Width, (INTN)gGfx.Height);
}

VOID
GfxFillRect (
  INTN    X,
  INTN    Y,
  INTN    W,
  INTN    H,
  UINT32  Rgb
  )
{
  INTN                           X0, Y0, X1, Y1, Yy, Xx;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  P;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Row;

  X0 = MAX (X, gGfx.ClipX0);
  Y0 = MAX (Y, gGfx.ClipY0);
  X1 = MIN (X + W, gGfx.ClipX1);
  Y1 = MIN (Y + H, gGfx.ClipY1);
  if ((X0 >= X1) || (Y0 >= Y1)) {
    return;
  }

  P = Px (Rgb);
  for (Yy = Y0; Yy < Y1; Yy++) {
    Row = gGfx.Back + (UINTN)Yy * gGfx.Width;
    for (Xx = X0; Xx < X1; Xx++) {
      Row[Xx] = P;
    }
  }
}

VOID
GfxHLine (
  INTN    X,
  INTN    Y,
  INTN    W,
  UINT32  Rgb
  )
{
  GfxFillRect (X, Y, W, 1, Rgb);
}

VOID
GfxVLine (
  INTN    X,
  INTN    Y,
  INTN    H,
  UINT32  Rgb
  )
{
  GfxFillRect (X, Y, 1, H, Rgb);
}

VOID
GfxFrameRect (
  INTN    X,
  INTN    Y,
  INTN    W,
  INTN    H,
  UINT32  Rgb
  )
{
  GfxHLine (X, Y, W, Rgb);
  GfxHLine (X, Y + H - 1, W, Rgb);
  GfxVLine (X, Y, H, Rgb);
  GfxVLine (X + W - 1, Y, H, Rgb);
}

VOID
GfxDrawChar (
  INTN    X,
  INTN    Y,
  CHAR16  Ch,
  UINT32  Fg
  )
{
  CONST UINT8                    *G;
  INTN                           Row, Col, Px0, Py;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  P;

  if (Ch == L' ') {
    return;
  }

  if ((X >= gGfx.ClipX1) || (Y >= gGfx.ClipY1) || (X + FONT_W <= gGfx.ClipX0) || (Y + FONT_H <= gGfx.ClipY0)) {
    return;
  }

  G = FindGlyph (Ch);
  if (G == NULL) {
    return;
  }

  P = Px (Fg);
  for (Row = 0; Row < FONT_H; Row++) {
    Py = Y + Row;
    if ((Py < gGfx.ClipY0) || (Py >= gGfx.ClipY1) || (G[Row] == 0)) {
      continue;
    }

    for (Col = 0; Col < FONT_W; Col++) {
      if ((G[Row] & (0x80 >> Col)) == 0) {
        continue;
      }

      Px0 = X + Col;
      if ((Px0 >= gGfx.ClipX0) && (Px0 < gGfx.ClipX1)) {
        gGfx.Back[(UINTN)Py * gGfx.Width + (UINTN)Px0] = P;
      }
    }
  }
}

INTN
GfxDrawTextN (
  INTN          X,
  INTN          Y,
  CONST CHAR16  *Str,
  UINTN         Len,
  UINT32        Fg
  )
{
  UINTN  Index;

  for (Index = 0; Index < Len && Str[Index] != 0; Index++) {
    GfxDrawChar (X, Y, Str[Index], Fg);
    X += FONT_W;
  }

  return X;
}

INTN
GfxDrawText (
  INTN          X,
  INTN          Y,
  CONST CHAR16  *Str,
  UINT32        Fg
  )
{
  return GfxDrawTextN (X, Y, Str, StrLen (Str), Fg);
}

/**
  Small filled triangle used on scroll bar buttons.
  Dir: 0 up, 1 down, 2 left, 3 right.
**/
VOID
GfxDrawArrow (
  INTN    Cx,
  INTN    Cy,
  UINTN   Dir,
  UINT32  Rgb
  )
{
  INTN  I;

  for (I = 0; I < 4; I++) {
    switch (Dir) {
      case 0:
        GfxHLine (Cx - I, Cy - 2 + I, 2 * I + 1, Rgb);
        break;
      case 1:
        GfxHLine (Cx - I, Cy + 1 - I, 2 * I + 1, Rgb);
        break;
      case 2:
        GfxVLine (Cx - 2 + I, Cy - I, 2 * I + 1, Rgb);
        break;
      default:
        GfxVLine (Cx + 1 - I, Cy - I, 2 * I + 1, Rgb);
        break;
    }
  }
}

STATIC
VOID
BltBack (
  INTN  X,
  INTN  Y,
  INTN  W,
  INTN  H
  )
{
  if (X < 0) {
    W += X;
    X  = 0;
  }

  if (Y < 0) {
    H += Y;
    Y  = 0;
  }

  if (X + W > (INTN)gGfx.Width) {
    W = (INTN)gGfx.Width - X;
  }

  if (Y + H > (INTN)gGfx.Height) {
    H = (INTN)gGfx.Height - Y;
  }

  if ((W <= 0) || (H <= 0)) {
    return;
  }

  gGfx.Gop->Blt (
              gGfx.Gop,
              gGfx.Back,
              EfiBltBufferToVideo,
              (UINTN)X,
              (UINTN)Y,
              (UINTN)X,
              (UINTN)Y,
              (UINTN)W,
              (UINTN)H,
              gGfx.Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
              );
}

STATIC
VOID
PaintMouse (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Tmp[CUR_W * CUR_H];
  CONST CHAR8                    **Shape;
  INTN                           Ox, Oy, R, C, Sx, Sy;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black, White;

  Shape = mMouseIBeam ? mIBeam : mArrow;
  Ox    = mMouseIBeam ? mMouseX - 5 : mMouseX;
  Oy    = mMouseIBeam ? mMouseY - 9 : mMouseY;
  Black = Px (0x000000);
  White = Px (0xFFFFFF);

  for (R = 0; R < CUR_H; R++) {
    for (C = 0; C < CUR_W; C++) {
      Sx = Ox + C;
      Sy = Oy + R;
      if ((Sx >= 0) && (Sy >= 0) && (Sx < (INTN)gGfx.Width) && (Sy < (INTN)gGfx.Height)) {
        Tmp[R * CUR_W + C] = gGfx.Back[(UINTN)Sy * gGfx.Width + (UINTN)Sx];
      } else {
        ZeroMem (&Tmp[R * CUR_W + C], sizeof (Tmp[0]));
      }

      if (Shape[R][C] == '#') {
        Tmp[R * CUR_W + C] = mMouseIBeam ? White : Black;
      } else if (Shape[R][C] == '.') {
        Tmp[R * CUR_W + C] = mMouseIBeam ? Black : White;
      }
    }
  }

  //
  // Clip to the screen, then blt only the visible part.
  //
  {
    INTN  X0, Y0, X1, Y1;

    X0 = MAX (Ox, 0);
    Y0 = MAX (Oy, 0);
    X1 = MIN (Ox + CUR_W, (INTN)gGfx.Width);
    Y1 = MIN (Oy + CUR_H, (INTN)gGfx.Height);
    if ((X0 < X1) && (Y0 < Y1)) {
      gGfx.Gop->Blt (
                  gGfx.Gop,
                  Tmp,
                  EfiBltBufferToVideo,
                  (UINTN)(X0 - Ox),
                  (UINTN)(Y0 - Oy),
                  (UINTN)X0,
                  (UINTN)Y0,
                  (UINTN)(X1 - X0),
                  (UINTN)(Y1 - Y0),
                  CUR_W * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                  );
    }
  }
}

VOID
GfxEraseMouse (
  VOID
  )
{
  if (mMouseShown) {
    if (mMouseIBeam) {
      BltBack (mMouseX - 5, mMouseY - 9, CUR_W, CUR_H);
    } else {
      BltBack (mMouseX, mMouseY, CUR_W, CUR_H);
    }

    mMouseShown = FALSE;
  }
}

VOID
GfxDrawMouse (
  INTN     X,
  INTN     Y,
  BOOLEAN  IBeam
  )
{
  if (mMouseShown && (X == mMouseX) && (Y == mMouseY) && (IBeam == mMouseIBeam)) {
    return;
  }

  GfxEraseMouse ();
  mMouseX     = X;
  mMouseY     = Y;
  mMouseIBeam = IBeam;
  mMouseShown = TRUE;
  PaintMouse ();
}

VOID
GfxPresent (
  VOID
  )
{
  BltBack (0, 0, (INTN)gGfx.Width, (INTN)gGfx.Height);
  if (mMouseShown) {
    PaintMouse ();
  }
}

VOID
GfxPresentRect (
  INTN  X,
  INTN  Y,
  INTN  W,
  INTN  H
  )
{
  BltBack (X, Y, W, H);
  if (mMouseShown) {
    PaintMouse ();
  }
}
