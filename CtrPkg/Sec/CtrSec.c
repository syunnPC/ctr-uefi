/** @file
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <PiPei.h>
#include <CtrPlatform.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/CtrProgressLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/HobLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/PrePiLib.h>

#define SECTION_SIZE  SIZE_1MB
#define SECTION_RW    (BIT10 | BIT1)
#define SECTION_NC    BIT12
#define SECTION_XN    BIT4
#define SECTION_WB    (BIT12 | BIT3 | BIT2)

STATIC
VOID
MapSections (
  IN UINT32  VirtualAddress,
  IN UINT32  PhysicalAddress,
  IN UINT32  Length,
  IN UINT32  Attributes
  )
{
  UINT32  *Table;
  UINT32  Index;

  Table = (UINT32 *)(UINTN)CTR_TT_BASE;
  for (Index = 0; Index < Length / SECTION_SIZE; Index++) {
    Table[VirtualAddress / SECTION_SIZE + Index] =
      (PhysicalAddress + Index * SECTION_SIZE) | SECTION_RW | Attributes;
  }
}

VOID
CtrSecMain (
  VOID
  )
{
  EFI_HOB_HANDOFF_INFO_TABLE  *Hobs;

  ZeroMem ((VOID *)(UINTN)CTR_TT_BASE, SIZE_16KB);
  MapSections (0xFFF00000, CTR_VECTOR_SECTION, SECTION_SIZE, SECTION_NC);
  MapSections (0x10000000, 0x10000000, 0x00600000, SECTION_XN);
  MapSections (CTR_GIC_DIST & ~(SECTION_SIZE - 1), CTR_GIC_DIST & ~(SECTION_SIZE - 1), SECTION_SIZE, SECTION_XN);
  MapSections (0x18000000, 0x18000000, 0x00600000, SECTION_XN);
  MapSections (0x1FF00000, 0x1FF00000, SECTION_SIZE, SECTION_XN);
  MapSections (0x20000000, 0x20000000, 0x08000000, SECTION_NC);
  MapSections (CTR_RAM_BASE, CTR_RAM_BASE, CTR_RAM_END - CTR_RAM_BASE, SECTION_WB);

  ArmSetTTBR0 ((VOID *)(UINTN)CTR_TT_BASE);
  ArmSetDomainAccessControl (1);
  ArmInvalidateInstructionAndDataTlb ();
  ArmInvalidateInstructionCache ();
  ArmInvalidateDataCache ();
  ArmDataSyncronizationBarrier ();
  ArmEnableMmu ();
  ArmInstructionSynchronizationBarrier ();
  ArmEnableInstructionCache ();
  ArmEnableDataCache ();
  CtrShowBootProgress (CtrProgressSec);

  Hobs = HobConstructor (
           (VOID *)(UINTN)CTR_RAM_BASE,
           CTR_RAM_END - CTR_RAM_BASE,
           (VOID *)(UINTN)CTR_RAM_BASE,
           (VOID *)(UINTN)CTR_STACK_BASE
           );
  PrePeiSetHobList (Hobs);
  SetBootMode (BOOT_WITH_FULL_CONFIGURATION);
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_TESTED | EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE | EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE,
    CTR_RAM_BASE,
    CTR_RAM_END - CTR_RAM_BASE
    );
  BuildStackHob (CTR_STACK_BASE, CTR_STACK_SIZE);
  BuildCpuHob (32, 0);
  BuildFvHob (CTR_FD_BASE, CTR_FD_SIZE);

  LoadDxeCoreFromFv (NULL, 0);
  CpuDeadLoop ();
}
