/** @file
  Load and save text files. Uses the UEFI Shell protocol when running under
  the Shell (paths like fs0:\dir\a.txt or relative to the current directory),
  otherwise the file system the application was loaded from.

  Supported encodings: UTF-8 (with or without BOM), UTF-16 LE/BE (BOM),
  and ANSI (Latin-1, one byte per character).

  Copyright (c) 2026, weinet000210. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Notepad.h"

STATIC EFI_SHELL_PROTOCOL  *mShell;
STATIC EFI_FILE_PROTOCOL   *mRoot;

STATIC CONST CHAR16  *mEncNames[ENC_COUNT] = {
  L"UTF-8",
  L"UTF-8 BOM",
  L"UTF-16 LE",
  L"UTF-16 BE",
  L"ANSI"
};

CONST CHAR16 *
EncName (
  UINTN  Enc
  )
{
  return (Enc < ENC_COUNT) ? mEncNames[Enc] : L"?";
}

EFI_STATUS
FileIoInit (
  EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *Li;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;

  Status = gBS->LocateProtocol (&gEfiShellProtocolGuid, NULL, (VOID **)&mShell);
  if (!EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  mShell = NULL;
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (Li->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Fs->OpenVolume (Fs, &mRoot);
}

STATIC
VOID
NormalizePath (
  CONST CHAR16  *In,
  CHAR16        *Out,
  UINTN         OutLen
  )
{
  UINTN  I;

  for (I = 0; I + 1 < OutLen && In[I] != 0; I++) {
    Out[I] = (In[I] == L'/') ? L'\\' : In[I];
  }

  Out[I] = 0;
}

STATIC
EFI_STATUS
ReadAll (
  CONST CHAR16  *Path,
  UINT8         **Data,
  UINTN         *Size
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Sh;
  EFI_FILE_PROTOCOL  *F;
  UINT64             Sz;
  UINTN              Rd;
  EFI_FILE_INFO      *Info;
  UINTN              InfoSize;
  CHAR16             P[MAX_PATH];

  *Data = NULL;
  *Size = 0;
  NormalizePath (Path, P, MAX_PATH);

  if (mShell != NULL) {
    Status = mShell->OpenFileByName (P, &Sh, EFI_FILE_MODE_READ);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = mShell->GetFileSize (Sh, &Sz);
    if (!EFI_ERROR (Status)) {
      Rd    = (UINTN)Sz;
      *Data = AllocatePool (Rd + 4);
      if (*Data == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
      } else {
        Status = mShell->ReadFile (Sh, &Rd, *Data);
        *Size  = Rd;
      }
    }

    mShell->CloseFile (Sh);
  } else {
    if (mRoot == NULL) {
      return EFI_NOT_READY;
    }

    Status = mRoot->Open (mRoot, &F, P, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    InfoSize = 0;
    Info     = NULL;
    Status   = F->GetInfo (F, &gEfiFileInfoGuid, &InfoSize, NULL);
    if (Status == EFI_BUFFER_TOO_SMALL) {
      Info   = AllocatePool (InfoSize);
      Status = (Info == NULL) ? EFI_OUT_OF_RESOURCES : F->GetInfo (F, &gEfiFileInfoGuid, &InfoSize, Info);
    }

    if (!EFI_ERROR (Status)) {
      Rd    = (UINTN)Info->FileSize;
      *Data = AllocatePool (Rd + 4);
      if (*Data == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
      } else {
        Status = F->Read (F, &Rd, *Data);
        *Size  = Rd;
      }
    }

    if (Info != NULL) {
      FreePool (Info);
    }

    F->Close (F);
  }

  if (EFI_ERROR (Status) && (*Data != NULL)) {
    FreePool (*Data);
    *Data = NULL;
  }

  return Status;
}

STATIC
EFI_STATUS
WriteAll (
  CONST CHAR16  *Path,
  UINT8         *Data,
  UINTN         Size
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Sh;
  EFI_FILE_PROTOCOL  *F;
  UINTN              Wr;
  CHAR16             P[MAX_PATH];

  NormalizePath (Path, P, MAX_PATH);

  if (mShell != NULL) {
    //
    // Delete first so a shorter file does not keep the old tail.
    //
    Status = mShell->OpenFileByName (P, &Sh, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE);
    if (!EFI_ERROR (Status)) {
      mShell->DeleteFile (Sh);
    }

    Status = mShell->CreateFile (P, 0, &Sh);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Wr     = Size;
    Status = mShell->WriteFile (Sh, &Wr, Data);
    mShell->CloseFile (Sh);
    return Status;
  }

  if (mRoot == NULL) {
    return EFI_NOT_READY;
  }

  Status = mRoot->Open (mRoot, &F, P, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status)) {
    F->Delete (F);
  }

  Status = mRoot->Open (mRoot, &F, P, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Wr     = Size;
  Status = F->Write (F, &Wr, Data);
  F->Close (F);
  return Status;
}

BOOLEAN
FileExists (
  CONST CHAR16  *Path
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Sh;
  EFI_FILE_PROTOCOL  *F;
  CHAR16             P[MAX_PATH];

  NormalizePath (Path, P, MAX_PATH);
  if (mShell != NULL) {
    Status = mShell->OpenFileByName (P, &Sh, EFI_FILE_MODE_READ);
    if (!EFI_ERROR (Status)) {
      mShell->CloseFile (Sh);
      return TRUE;
    }

    return FALSE;
  }

  if (mRoot == NULL) {
    return FALSE;
  }

  Status = mRoot->Open (mRoot, &F, P, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR (Status)) {
    F->Close (F);
    return TRUE;
  }

  return FALSE;
}

/**
  Decode one UTF-8 sequence. Returns the number of bytes used, 0 if invalid.
**/
STATIC
UINTN
Utf8Next (
  CONST UINT8  *S,
  UINTN        N,
  UINT32       *Cp
  )
{
  UINT8  C;

  C = S[0];
  if (C < 0x80) {
    *Cp = C;
    return 1;
  }

  if (((C & 0xE0) == 0xC0) && (N >= 2) && ((S[1] & 0xC0) == 0x80)) {
    *Cp = ((UINT32)(C & 0x1F) << 6) | (S[1] & 0x3F);
    return (*Cp >= 0x80) ? 2 : 0;
  }

  if (((C & 0xF0) == 0xE0) && (N >= 3) && ((S[1] & 0xC0) == 0x80) && ((S[2] & 0xC0) == 0x80)) {
    *Cp = ((UINT32)(C & 0x0F) << 12) | ((UINT32)(S[1] & 0x3F) << 6) | (S[2] & 0x3F);
    return (*Cp >= 0x800) ? 3 : 0;
  }

  if (((C & 0xF8) == 0xF0) && (N >= 4) && ((S[1] & 0xC0) == 0x80) &&
      ((S[2] & 0xC0) == 0x80) && ((S[3] & 0xC0) == 0x80))
  {
    *Cp = ((UINT32)(C & 0x07) << 18) | ((UINT32)(S[1] & 0x3F) << 12) |
          ((UINT32)(S[2] & 0x3F) << 6) | (S[3] & 0x3F);
    return ((*Cp >= 0x10000) && (*Cp <= 0x10FFFF)) ? 4 : 0;
  }

  return 0;
}

STATIC
BOOLEAN
IsValidUtf8 (
  CONST UINT8  *S,
  UINTN        N
  )
{
  UINTN   I, Used;
  UINT32  Cp;

  for (I = 0; I < N; I += Used) {
    Used = Utf8Next (S + I, N - I, &Cp);
    if (Used == 0) {
      return FALSE;
    }
  }

  return TRUE;
}

EFI_STATUS
FileLoad (
  CONST CHAR16  *Path,
  DOC           *Doc,
  UINTN         *Enc,
  UINTN         *Eol
  )
{
  EFI_STATUS  Status;
  UINT8       *Data;
  UINTN       Size, I, Used, Out, Crlf, Lf;
  CHAR16      *Txt;
  UINT32      Cp;
  CHAR16      Ch;
  UINTN       E;

  Status = ReadAll (Path, &Data, &Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Txt = AllocatePool ((Size + 1) * sizeof (CHAR16));
  if (Txt == NULL) {
    FreePool (Data);
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Pass 1: decode bytes into raw UCS-2 (CR/LF untouched).
  //
  Out = 0;
  I   = 0;
  if ((Size >= 2) && (Data[0] == 0xFF) && (Data[1] == 0xFE)) {
    E = ENC_UTF16LE;
    for (I = 2; I + 1 < Size; I += 2) {
      Txt[Out++] = (CHAR16)(Data[I] | (Data[I + 1] << 8));
    }
  } else if ((Size >= 2) && (Data[0] == 0xFE) && (Data[1] == 0xFF)) {
    E = ENC_UTF16BE;
    for (I = 2; I + 1 < Size; I += 2) {
      Txt[Out++] = (CHAR16)((Data[I] << 8) | Data[I + 1]);
    }
  } else {
    if ((Size >= 3) && (Data[0] == 0xEF) && (Data[1] == 0xBB) && (Data[2] == 0xBF)) {
      E = ENC_UTF8BOM;
      I = 3;
    } else {
      E = IsValidUtf8 (Data, Size) ? ENC_UTF8 : ENC_ANSI;
    }

    if (E == ENC_ANSI) {
      for (I = 0; I < Size; I++) {
        Txt[Out++] = Data[I];
      }
    } else {
      while (I < Size) {
        Used = Utf8Next (Data + I, Size - I, &Cp);
        if (Used == 0) {
          Cp   = 0xFFFD;
          Used = 1;
        }

        Txt[Out++] = (Cp > 0xFFFF) ? (CHAR16)0xFFFD : (CHAR16)Cp;
        I         += Used;
      }
    }
  }

  FreePool (Data);

  //
  // Pass 2: fold CRLF / CR / LF into '\n' in place and pick the EOL style.
  //
  Crlf = 0;
  Lf   = 0;
  Size = Out;
  Out  = 0;
  for (I = 0; I < Size; I++) {
    Ch = Txt[I];
    if (Ch == L'\r') {
      if ((I + 1 < Size) && (Txt[I + 1] == L'\n')) {
        I++;
      }

      Crlf++;
      Txt[Out++] = L'\n';
    } else if (Ch == L'\n') {
      Lf++;
      Txt[Out++] = L'\n';
    } else if (Ch != 0) {
      Txt[Out++] = Ch;
    }
  }

  DocFree (Doc);
  DocInit (Doc);
  {
    POS  Zero;

    Zero.Line = 0;
    Zero.Col  = 0;
    DocInsert (Doc, Zero, Txt, Out, NULL);
  }

  FreePool (Txt);
  *Enc = E;
  *Eol = (Lf > Crlf) ? EOL_LF : EOL_CRLF;
  return EFI_SUCCESS;
}

STATIC
UINTN
PutChar (
  UINT8   *Buf,
  UINTN   Enc,
  CHAR16  Ch
  )
{
  switch (Enc) {
    case ENC_UTF16LE:
      Buf[0] = (UINT8)(Ch & 0xFF);
      Buf[1] = (UINT8)(Ch >> 8);
      return 2;
    case ENC_UTF16BE:
      Buf[0] = (UINT8)(Ch >> 8);
      Buf[1] = (UINT8)(Ch & 0xFF);
      return 2;
    case ENC_ANSI:
      Buf[0] = (Ch < 0x100) ? (UINT8)Ch : (UINT8)'?';
      return 1;
    default:
      if (Ch < 0x80) {
        Buf[0] = (UINT8)Ch;
        return 1;
      }

      if (Ch < 0x800) {
        Buf[0] = (UINT8)(0xC0 | (Ch >> 6));
        Buf[1] = (UINT8)(0x80 | (Ch & 0x3F));
        return 2;
      }

      Buf[0] = (UINT8)(0xE0 | (Ch >> 12));
      Buf[1] = (UINT8)(0x80 | ((Ch >> 6) & 0x3F));
      Buf[2] = (UINT8)(0x80 | (Ch & 0x3F));
      return 3;
  }
}

EFI_STATUS
FileSave (
  CONST CHAR16  *Path,
  DOC           *Doc,
  UINTN         Enc,
  UINTN         Eol
  )
{
  UINTN       Chars, L, C, Out;
  UINT8       *Buf;
  EFI_STATUS  Status;

  Chars = 0;
  for (L = 0; L < Doc->Count; L++) {
    Chars += Doc->Lines[L].Len + 2;
  }

  Buf = AllocatePool (Chars * 3 + 4);
  if (Buf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Out = 0;
  if (Enc == ENC_UTF8BOM) {
    Buf[Out++] = 0xEF;
    Buf[Out++] = 0xBB;
    Buf[Out++] = 0xBF;
  } else if (Enc == ENC_UTF16LE) {
    Buf[Out++] = 0xFF;
    Buf[Out++] = 0xFE;
  } else if (Enc == ENC_UTF16BE) {
    Buf[Out++] = 0xFE;
    Buf[Out++] = 0xFF;
  }

  for (L = 0; L < Doc->Count; L++) {
    for (C = 0; C < Doc->Lines[L].Len; C++) {
      Out += PutChar (Buf + Out, Enc, Doc->Lines[L].Text[C]);
    }

    if (L + 1 < Doc->Count) {
      if (Eol == EOL_CRLF) {
        Out += PutChar (Buf + Out, Enc, L'\r');
      }

      Out += PutChar (Buf + Out, Enc, L'\n');
    }
  }

  Status = WriteAll (Path, Buf, Out);
  FreePool (Buf);
  return Status;
}
