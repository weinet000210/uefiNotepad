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
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *
ResolveVolume (
  CONST CHAR16  *Path,
  CONST CHAR16  **Rest
  );

/**
  Open a file without the Shell: "FSn:\path" picks that volume, any other
  path is relative to the volume the application was loaded from.
**/
STATIC
EFI_STATUS
SfsOpen (
  CONST CHAR16       *Path,
  EFI_FILE_PROTOCOL  **F,
  UINT64             Mode
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  CONST CHAR16                     *Rest;
  EFI_FILE_PROTOCOL                *Root;
  EFI_STATUS                       Status;

  Fs = ResolveVolume (Path, &Rest);
  if (Fs == NULL) {
    if (mRoot == NULL) {
      return EFI_NOT_READY;
    }

    return mRoot->Open (mRoot, F, (CHAR16 *)Path, Mode, 0);
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Root->Open (Root, F, (CHAR16 *)Rest, Mode, 0);
  Root->Close (Root);
  return Status;
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
    Status = SfsOpen (P, &F, EFI_FILE_MODE_READ);
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

  Status = SfsOpen (P, &F, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE);
  if (!EFI_ERROR (Status)) {
    F->Delete (F);
  }

  Status = SfsOpen (P, &F, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE);
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

  Status = SfsOpen (P, &F, EFI_FILE_MODE_READ);
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

//
// ---------------- Volumes and directories (file browser) ----------------
//
STATIC VOLUME_INFO  mVols[MAX_VOLUMES];
STATIC UINTN        mVolCount;

STATIC
VOID
CopyMapName (
  CONST CHAR16  *Map,
  CHAR16        *Out,
  UINTN         OutLen
  )
{
  CONST CHAR16  *P;
  CONST CHAR16  *Best;
  UINTN         I;

  //
  // Map looks like "FS0:;BLK3:". Prefer the FSn: alias.
  //
  Best = Map;
  for (P = Map; *P != 0; P++) {
    if (((P == Map) || (P[-1] == L';')) && ((P[0] | 0x20) == L'f') && ((P[1] | 0x20) == L's')) {
      Best = P;
      break;
    }
  }

  for (I = 0; I + 1 < OutLen && Best[I] != 0 && Best[I] != L';'; I++) {
    Out[I] = Best[I];
  }

  Out[I] = 0;
}

STATIC
INTN
VolNameCmp (
  CONST CHAR16  *A,
  CONST CHAR16  *B
  )
{
  UINTN  La, Lb;

  // FS2: before FS10:
  La = StrLen (A);
  Lb = StrLen (B);
  if (La != Lb) {
    return (La < Lb) ? -1 : 1;
  }

  return StrCmp (A, B);
}

UINTN
FileListVolumes (
  VOLUME_INFO  *Vols,
  UINTN        Max
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     Count, I, J;
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  CONST CHAR16              *Map;
  EFI_FILE_PROTOCOL         *Root;
  EFI_FILE_SYSTEM_INFO      *Info;
  UINTN                     InfoSize;
  VOLUME_INFO               V;

  mVolCount = 0;
  Status    = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  for (I = 0; I < Count && mVolCount < MAX_VOLUMES; I++) {
    ZeroMem (&V, sizeof (V));
    if (EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&V.Fs))) {
      continue;
    }

    Map = NULL;
    if ((mShell != NULL) &&
        !EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp)))
    {
      Map = mShell->GetMapFromDevicePath (&Dp);
    }

    if (Map != NULL) {
      CopyMapName (Map, V.Name, ARRAY_SIZE (V.Name));
    } else {
      UnicodeSPrint (V.Name, sizeof (V.Name), L"FS%d:", mVolCount);
    }

    if (!EFI_ERROR (V.Fs->OpenVolume (V.Fs, &Root))) {
      InfoSize = SIZE_OF_EFI_FILE_SYSTEM_INFO + 256;
      Info     = AllocateZeroPool (InfoSize);
      if ((Info != NULL) && !EFI_ERROR (Root->GetInfo (Root, &gEfiFileSystemInfoGuid, &InfoSize, Info))) {
        StrnCpyS (V.Label, ARRAY_SIZE (V.Label), Info->VolumeLabel, ARRAY_SIZE (V.Label) - 1);
        V.Size     = Info->VolumeSize;
        V.Free     = Info->FreeSpace;
        V.ReadOnly = Info->ReadOnly;
      }

      if (Info != NULL) {
        FreePool (Info);
      }

      Root->Close (Root);
    }

    //
    // Insert sorted by name.
    //
    for (J = mVolCount; J > 0 && VolNameCmp (mVols[J - 1].Name, V.Name) > 0; J--) {
      mVols[J] = mVols[J - 1];
    }

    mVols[J] = V;
    mVolCount++;
  }

  FreePool (Handles);
  for (I = 0; I < mVolCount && I < Max; I++) {
    Vols[I] = mVols[I];
  }

  return MIN (mVolCount, Max);
}

/**
  Split "FS0:\dir\x" into its volume and the path inside it.
**/
STATIC
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *
ResolveVolume (
  CONST CHAR16  *Path,
  CONST CHAR16  **Rest
  )
{
  UINTN  I, N;

  if (mVolCount == 0) {
    FileListVolumes (NULL, 0);
  }

  for (I = 0; I < mVolCount; I++) {
    N = StrLen (mVols[I].Name);
    if (StrnCmp (Path, mVols[I].Name, N) == 0) {
      *Rest = Path + N;
      return mVols[I].Fs;
    }

    //
    // Case-insensitive fallback ("fs0:").
    //
    {
      UINTN  K;

      for (K = 0; K < N; K++) {
        if ((Path[K] | 0x20) != (mVols[I].Name[K] | 0x20)) {
          break;
        }
      }

      if (K == N) {
        *Rest = Path + N;
        return mVols[I].Fs;
      }
    }
  }

  return NULL;
}

STATIC
BOOLEAN
NameLess (
  DIR_ENTRY  *A,
  DIR_ENTRY  *B
  )
{
  UINTN   I;
  CHAR16  Ca, Cb;

  if (A->Dir != B->Dir) {
    return A->Dir;
  }

  for (I = 0; ; I++) {
    Ca = A->Name[I];
    Cb = B->Name[I];
    if ((Ca >= L'A') && (Ca <= L'Z')) {
      Ca = (CHAR16)(Ca + 32);
    }

    if ((Cb >= L'A') && (Cb <= L'Z')) {
      Cb = (CHAR16)(Cb + 32);
    }

    if ((Ca != Cb) || (Ca == 0)) {
      return Ca < Cb;
    }
  }
}

EFI_STATUS
FileListDir (
  CONST CHAR16  *Dir,
  DIR_ENTRY     **Entries,
  UINTN         *Count
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  CONST CHAR16                     *Rest;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *D;
  EFI_STATUS                       Status;
  EFI_FILE_INFO                    *Info;
  UINTN                            Size, N, Cap, I, J;
  DIR_ENTRY                        *Out;
  DIR_ENTRY                        *Nw;
  DIR_ENTRY                        T;
  CHAR16                           P[MAX_PATH];

  *Entries = NULL;
  *Count   = 0;
  Fs       = ResolveVolume (Dir, &Rest);
  if (Fs == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NormalizePath ((*Rest == 0) ? L"\\" : Rest, P, MAX_PATH);
  Status = Root->Open (Root, &D, P, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Root->Close (Root);
    return Status;
  }

  Info = AllocatePool (SIZE_OF_EFI_FILE_INFO + 512 * sizeof (CHAR16));
  Out  = NULL;
  N    = 0;
  Cap  = 0;
  while (Info != NULL) {
    Size   = SIZE_OF_EFI_FILE_INFO + 512 * sizeof (CHAR16);
    Status = D->Read (D, &Size, Info);
    if (EFI_ERROR (Status) || (Size == 0)) {
      break;
    }

    if ((StrCmp (Info->FileName, L".") == 0) || (StrCmp (Info->FileName, L"..") == 0)) {
      continue;
    }

    if (N == Cap) {
      Cap = (Cap == 0) ? 64 : Cap * 2;
      Nw  = AllocatePool (Cap * sizeof (DIR_ENTRY));
      if (Nw == NULL) {
        break;
      }

      if (Out != NULL) {
        CopyMem (Nw, Out, N * sizeof (DIR_ENTRY));
        FreePool (Out);
      }

      Out = Nw;
    }

    ZeroMem (&Out[N], sizeof (DIR_ENTRY));
    StrnCpyS (Out[N].Name, ARRAY_SIZE (Out[N].Name), Info->FileName, ARRAY_SIZE (Out[N].Name) - 1);
    Out[N].Dir      = (Info->Attribute & EFI_FILE_DIRECTORY) != 0;
    Out[N].Size     = Info->FileSize;
    Out[N].Modified = Info->ModificationTime;
    N++;
  }

  if (Info != NULL) {
    FreePool (Info);
  }

  D->Close (D);
  Root->Close (Root);

  //
  // Insertion sort: folders first, then by name.
  //
  for (I = 1; I < N; I++) {
    T = Out[I];
    for (J = I; J > 0 && NameLess (&T, &Out[J - 1]); J--) {
      Out[J] = Out[J - 1];
    }

    Out[J] = T;
  }

  *Entries = Out;
  *Count   = N;
  return EFI_SUCCESS;
}

VOID
FileCurDir (
  CHAR16  *Buf,
  UINTN   BufLen
  )
{
  CONST CHAR16  *Cwd;

  Buf[0] = 0;
  if (mShell != NULL) {
    Cwd = mShell->GetCurDir (NULL);
    if (Cwd != NULL) {
      StrnCpyS (Buf, BufLen, Cwd, BufLen - 1);
    }
  }

  if ((Buf[0] == 0) && (mVolCount > 0)) {
    StrnCpyS (Buf, BufLen, mVols[0].Name, BufLen - 1);
  }
}
