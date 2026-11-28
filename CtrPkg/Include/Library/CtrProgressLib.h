/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#ifndef CTR_PROGRESS_LIB_H
#define CTR_PROGRESS_LIB_H

#include <Uefi.h>
#include <Protocol/DebugSupport.h>

typedef enum {
  CtrProgressSec,
  CtrProgressBds,
  CtrProgressSecEntry,
  CtrProgressPrePi,
  CtrProgressDxeLoad,
  CtrProgressDxeLoadReturned
} CTR_BOOT_PROGRESS;

typedef enum {
  CtrDxeCore,
  CtrDxeDispatch,
  CtrDxeStart,
  CtrDxeReturn,
  CtrDxeLoadError,
  CtrDxeMissingArch,
  CtrDxeMissingBds,
  CtrDxeBdsHandoff,
  CtrDxeBdsReturned
} CTR_DXE_PROGRESS;

VOID
EFIAPI
CtrShowBootProgress (
  IN CTR_BOOT_PROGRESS  Stage,
  IN EFI_STATUS         Status
  );

VOID
EFIAPI
CtrShowDxeProgress (
  IN CTR_DXE_PROGRESS  Stage,
  IN CONST EFI_GUID    *File OPTIONAL,
  IN EFI_STATUS        Status
  );

VOID
EFIAPI
CtrShowArmException (
  IN EFI_EXCEPTION_TYPE            Type,
  IN CONST EFI_SYSTEM_CONTEXT_ARM  *Context
  );

#endif
