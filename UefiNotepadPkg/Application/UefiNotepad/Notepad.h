/** @file
  uefiNotepad - a Notepad-style graphical text editor for the UEFI Shell.

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_NOTEPAD_H_
#define UEFI_NOTEPAD_H_

#include <Uefi.h>
#include <Uefi/UefiInternalFormRepresentation.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellParameters.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#define NP_VERSION  L"0.1.0"
#define NP_APPNAME  L"uefiNotepad"

//
// Font metrics (edk2 LaffStd 8x19 narrow glyphs).
//
#define FONT_W  8
#define FONT_H  19

//
// Layout metrics in pixels.
//
#define TITLE_H     24
#define MENU_H      22
#define STATUS_H    22
#define SCROLL_W    17
#define TEXT_PAD    4
#define TAB_SIZE    8
#define MAX_PATH    260
#define MAX_FIELD   256

//
// Colors (0xRRGGBB).
//
#define C_TITLE_BG     0x2B579A
#define C_TITLE_FG     0xFFFFFF
#define C_FACE         0xF0F0F0
#define C_FACE_DARK    0xD7D7D7
#define C_BORDER       0xADADAD
#define C_TEXT         0x000000
#define C_GRAYTEXT     0x6D6D6D
#define C_WINDOW       0xFFFFFF
#define C_HILITE       0x0078D7
#define C_HILITE_FG    0xFFFFFF
#define C_MENU_HOVER   0xCCE8FF
#define C_MENU_HOVER_B 0x99D1FF
#define C_THUMB        0xC2C2C2
#define C_THUMB_HOT    0xA6A6A6
#define C_BTN          0xE1E1E1

//
// ----- Graphics (Gfx.c) -----
//
typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL     *Gop;
  UINT32                           OrigMode;
  BOOLEAN                          ModeChanged;
  UINTN                            Width;
  UINTN                            Height;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Back;      // composed scene, no mouse cursor
  INTN                             ClipX0, ClipY0, ClipX1, ClipY1;
} GFX;

extern GFX  gGfx;

EFI_STATUS GfxInit (VOID);
VOID       GfxShutdown (VOID);
VOID       GfxSetClip (INTN X0, INTN Y0, INTN X1, INTN Y1);
VOID       GfxResetClip (VOID);
VOID       GfxFillRect (INTN X, INTN Y, INTN W, INTN H, UINT32 Rgb);
VOID       GfxFrameRect (INTN X, INTN Y, INTN W, INTN H, UINT32 Rgb);
VOID       GfxHLine (INTN X, INTN Y, INTN W, UINT32 Rgb);
VOID       GfxVLine (INTN X, INTN Y, INTN H, UINT32 Rgb);
VOID       GfxDrawChar (INTN X, INTN Y, CHAR16 Ch, UINT32 Fg);
INTN       GfxDrawText (INTN X, INTN Y, CONST CHAR16 *Str, UINT32 Fg);
INTN       GfxDrawTextN (INTN X, INTN Y, CONST CHAR16 *Str, UINTN Len, UINT32 Fg);
VOID       GfxDrawArrow (INTN Cx, INTN Cy, UINTN Dir, UINT32 Rgb);
VOID       GfxPresent (VOID);
VOID       GfxPresentRect (INTN X, INTN Y, INTN W, INTN H);
VOID       GfxDrawMouse (INTN X, INTN Y, BOOLEAN IBeam);
VOID       GfxEraseMouse (VOID);

//
// ----- Input (Input.c) -----
//
#define EV_NONE    0
#define EV_KEY     1
#define EV_MOUSE   2
#define EV_TIMER   3

#define MB_LEFT    0x01
#define MB_RIGHT   0x02

typedef struct {
  UINTN      Type;
  // key
  UINT16     Scan;
  CHAR16     Char;      // printable char, or 0
  BOOLEAN    Ctrl;
  BOOLEAN    Shift;
  BOOLEAN    Alt;
  // mouse
  INTN       X, Y;
  UINT32     Buttons;
  UINT32     Pressed;   // buttons that went down in this event
  UINT32     Released;  // buttons that went up in this event
  INTN       Wheel;
  BOOLEAN    DoubleClick;
} INPUT_EVENT;

typedef struct {
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *InEx;
  EFI_SIMPLE_POINTER_PROTOCOL        *Ptr;
  EFI_ABSOLUTE_POINTER_PROTOCOL      *Abs;
  EFI_EVENT                          Timer;
  BOOLEAN                            HasMouse;
  INTN                               MouseX, MouseY;
  UINT32                             Buttons;
  UINT64                             Ticks;
  UINT64                             LastClickTick;
  INTN                               LastClickX, LastClickY;
  BOOLEAN                            ShiftHeld;
} INPUT;

extern INPUT  gInput;

EFI_STATUS InputInit (VOID);
VOID       InputShutdown (VOID);
VOID       InputWait (INPUT_EVENT *Ev);

//
// ----- Document (Doc.c) -----
//
typedef struct {
  CHAR16    *Text;
  UINTN     Len;
  UINTN     Cap;
} LINE;

typedef struct {
  LINE     *Lines;
  UINTN    Count;
  UINTN    Cap;
} DOC;

typedef struct {
  UINTN    Line;
  UINTN    Col;
} POS;

VOID     DocInit (DOC *Doc);
VOID     DocFree (DOC *Doc);
VOID     DocInsert (DOC *Doc, POS At, CONST CHAR16 *Str, UINTN Len, POS *End);
VOID     DocDelete (DOC *Doc, POS Start, POS End);
CHAR16 * DocGetText (DOC *Doc, POS Start, POS End, UINTN *Len);
POS      DocEndPos (DOC *Doc);
INTN     PosCmp (POS A, POS B);
POS      PosAdvance (POS At, CONST CHAR16 *Str, UINTN Len);

//
// ----- File I/O (FileIo.c) -----
//
#define ENC_UTF8      0
#define ENC_UTF8BOM   1
#define ENC_UTF16LE   2
#define ENC_UTF16BE   3
#define ENC_ANSI      4
#define ENC_COUNT     5

#define EOL_CRLF      0
#define EOL_LF        1

EFI_STATUS FileIoInit (EFI_HANDLE ImageHandle);
EFI_STATUS FileLoad (CONST CHAR16 *Path, DOC *Doc, UINTN *Enc, UINTN *Eol);
EFI_STATUS FileSave (CONST CHAR16 *Path, DOC *Doc, UINTN Enc, UINTN Eol);
BOOLEAN    FileExists (CONST CHAR16 *Path);
CONST CHAR16 *EncName (UINTN Enc);

//
// ----- Editor (Editor.c / View.c / Dialog.c) -----
//
#define UNDO_INSERT  0
#define UNDO_DELETE  1

typedef struct {
  UINT8     Type;
  POS       At;
  CHAR16    *Text;
  UINTN     Len;
  UINTN     Group;
  POS       CursorBefore;
} UNDO_REC;

typedef struct {
  UNDO_REC  *Items;
  UINTN     Count;
  UINTN     Cap;
} UNDO_STACK;

// Hit-test targets
#define HIT_NONE        0
#define HIT_TEXT        1
#define HIT_MENUBAR     2
#define HIT_MENU        3
#define HIT_VSCROLL     4
#define HIT_HSCROLL     5
#define HIT_STATUS      6
#define HIT_TITLE       7

#define DRAG_NONE       0
#define DRAG_TEXT       1
#define DRAG_VTHUMB     2
#define DRAG_HTHUMB     3

typedef struct {
  DOC          Doc;
  POS          Cursor;
  POS          Anchor;           // selection anchor == Cursor when no selection
  UINTN        PreferredVCol;    // for up/down movement
  UINTN        TopLine;
  UINTN        LeftCol;          // in visual columns
  BOOLEAN      Modified;
  CHAR16       Path[MAX_PATH];
  UINTN        Enc;
  UINTN        Eol;
  BOOLEAN      ShowStatus;
  CHAR16       *Clip;
  UINTN        ClipLen;
  UNDO_STACK   Undo;
  UNDO_STACK   Redo;
  UINTN        Group;
  BOOLEAN      CanCoalesce;      // next typed char may join current undo group
  CHAR16       FindText[MAX_FIELD];
  CHAR16       ReplText[MAX_FIELD];
  BOOLEAN      MatchCase;
  BOOLEAN      Quit;
  BOOLEAN      CaretOn;
  UINT64       CaretTick;
  INTN         OpenMenu;         // -1 none
  INTN         MenuHot;          // highlighted item, -1 none
  BOOLEAN      MenuKeyboard;     // menu bar active via keyboard (F10/Alt)
  UINTN        Drag;
  INTN         DragOffset;
  UINTN        MaxCols;
  BOOLEAN      MaxColsDirty;
  CHAR16       Message[128];     // transient status message
  // layout (computed by ViewLayout)
  INTN         TextX, TextY, TextW, TextH;
  UINTN        Rows, Cols;
  BOOLEAN      HasVScroll, HasHScroll;
} EDITOR;

extern EDITOR  gEd;

// Editor.c
VOID     EdInit (VOID);
VOID     EdFree (VOID);
BOOLEAN  EdHasSel (VOID);
VOID     EdSelRange (POS *S, POS *E);
VOID     EdClearUndo (VOID);
VOID     EdNewGroup (VOID);
VOID     EdInsertText (CONST CHAR16 *Str, UINTN Len);
VOID     EdDeleteSel (VOID);
VOID     EdBackspace (VOID);
VOID     EdDeleteFwd (VOID);
VOID     EdUndo (VOID);
VOID     EdRedo (VOID);
VOID     EdCopy (VOID);
VOID     EdCut (VOID);
VOID     EdPaste (VOID);
VOID     EdSelectAll (VOID);
VOID     EdMoveCursor (POS P, BOOLEAN Extend);
VOID     EdMoveLeft (BOOLEAN Extend, BOOLEAN Word);
VOID     EdMoveRight (BOOLEAN Extend, BOOLEAN Word);
VOID     EdMoveUpDown (INTN Delta, BOOLEAN Extend);
VOID     EdHome (BOOLEAN Extend, BOOLEAN Doc);
VOID     EdEnd (BOOLEAN Extend, BOOLEAN Doc);
VOID     EdSelectWord (POS P);
BOOLEAN  EdFindNext (CONST CHAR16 *Needle, BOOLEAN Wrap);
UINTN    EdReplaceAll (CONST CHAR16 *Needle, CONST CHAR16 *Repl);
VOID     EdInsertTimeDate (VOID);
VOID     EdGotoLine (UINTN Line);
UINTN    EdVisCol (UINTN Line, UINTN Col);
UINTN    EdColFromVis (UINTN Line, UINTN VCol);
VOID     EdEnsureVisible (VOID);
VOID     EdSetMessage (CONST CHAR16 *Msg);

// View.c
VOID     ViewLayout (VOID);
VOID     ViewRender (VOID);
UINTN    ViewHitTest (INTN X, INTN Y);
BOOLEAN  ViewMouseToPos (INTN X, INTN Y, POS *P);
UINTN    ViewMaxCols (VOID);
VOID     ViewVScrollGeom (INTN *TrackY, INTN *TrackH, INTN *ThumbY, INTN *ThumbH);
VOID     ViewHScrollGeom (INTN *TrackX, INTN *TrackW, INTN *ThumbX, INTN *ThumbW);
VOID     ViewScrollTo (INTN Top);
VOID     ViewScrollLeftTo (INTN Left);

// Menu (View.c)
typedef struct {
  CONST CHAR16  *Label;     // NULL = separator
  CONST CHAR16  *Accel;
  UINTN         Cmd;
} MENU_ITEM;

typedef struct {
  CONST CHAR16     *Title;
  CHAR16           Hotkey;
  CONST MENU_ITEM  *Items;
  UINTN            Count;
} MENU;

extern CONST MENU  gMenus[];
extern CONST UINTN gMenuCount;

INTN     MenuTitleX (UINTN Index);
INTN     MenuTitleW (UINTN Index);
VOID     MenuPopupRect (UINTN Index, INTN *X, INTN *Y, INTN *W, INTN *H);
INTN     MenuItemAt (UINTN Index, INTN X, INTN Y);
CONST CHAR16 *MenuItemLabel (CONST MENU_ITEM *Item);

// Commands
enum {
  CMD_NONE = 0,
  CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_EXIT,
  CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DELETE,
  CMD_FIND, CMD_FINDNEXT, CMD_REPLACE, CMD_GOTO, CMD_SELECTALL, CMD_TIMEDATE,
  CMD_ENCODING, CMD_EOL, CMD_STATUSBAR,
  CMD_ABOUT, CMD_KEYS
};

VOID     RunCommand (UINTN Cmd);

// Dialog.c
#define DLG_OK      1
#define DLG_CANCEL  0
#define DLG_NO      2
#define DLG_ALT     3

INTN     DlgMessage (CONST CHAR16 *Title, CONST CHAR16 *Text, CONST CHAR16 *B1, CONST CHAR16 *B2, CONST CHAR16 *B3);
BOOLEAN  DlgInput (CONST CHAR16 *Title, CONST CHAR16 *Prompt, CHAR16 *Buf, UINTN BufLen);
INTN     DlgReplace (CHAR16 *Find, CHAR16 *Repl, UINTN BufLen);

// Main.c
VOID     FullRedraw (VOID);
VOID     UpdateMouseCursor (VOID);

#endif
