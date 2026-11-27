## @file
# SPDX-License-Identifier: GPL-2.0-or-later
##

[Defines]
  PLATFORM_NAME                  = Ctr
  PLATFORM_GUID                  = 719b0adc-2914-4353-8b44-ab919157524f
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/Ctr
  SUPPORTED_ARCHITECTURES        = ARM
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = CtrPkg/Ctr.fdf

[BuildOptions]
  GCC:*_*_ARM_PLATFORM_FLAGS == -march=armv6k -mfloat-abi=soft -ffile-prefix-map=$(WORKSPACE)=.
  GCC:*_*_ARM_CC_FLAGS = -marm -fno-pie -fno-common -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -Wno-error

[LibraryClasses.common]
  ArmDisassemblerLib|ArmPkg/Library/ArmDisassemblerLib/ArmDisassemblerLib.inf
  ArmLib|ArmPkg/Library/ArmLib/Arm11/Arm11Lib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  CtrProgressLib|CtrPkg/Library/CtrProgressLib/CtrProgressLib.inf
  CpuExceptionHandlerLib|MdeModulePkg/Library/CpuExceptionHandlerLibNull/CpuExceptionHandlerLibNull.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  DebugAgentLib|MdeModulePkg/Library/DebugAgentLibNull/DebugAgentLibNull.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  EfiResetSystemLib|CtrPkg/Library/CtrResetLib/CtrResetLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  RealTimeClockLib|CtrPkg/Library/CtrRtcLib/CtrRtcLib.inf
  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  SecurityManagementLib|MdeModulePkg/Library/DxeSecurityManagementLib/DxeSecurityManagementLib.inf
  SerialPortLib|MdePkg/Library/BaseSerialPortLibNull/BaseSerialPortLibNull.inf
  TimerLib|CtrPkg/Library/CtrTimerLib/CtrTimerLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiDecompressLib|MdePkg/Library/BaseUefiDecompressLib/BaseUefiDecompressLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[LibraryClasses.common.SEC]
  ArmLib|ArmPkg/Library/ArmLib/Arm11/Arm11LibSec.inf
  ExtractGuidedSectionLib|EmbeddedPkg/Library/PrePiExtractGuidedSectionLib/PrePiExtractGuidedSectionLib.inf
  HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
  MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
  PrePiHobListPointerLib|EmbeddedPkg/Library/PrePiHobListPointerLib/PrePiHobListPointerLib.inf
  PrePiLib|EmbeddedPkg/Library/PrePiLib/PrePiLib.inf

[LibraryClasses.common.DXE_CORE]
  DxeCoreEntryPoint|MdePkg/Library/DxeCoreEntryPoint/DxeCoreEntryPoint.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf
  HobLib|MdePkg/Library/DxeCoreHobLib/DxeCoreHobLib.inf
  MemoryAllocationLib|MdeModulePkg/Library/DxeCoreMemoryAllocationLib/DxeCoreMemoryAllocationLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf

[PcdsFeatureFlag.common]
  gArmTokenSpaceGuid.PcdRelocateVectorTable|TRUE
  gArmTokenSpaceGuid.PcdDebuggerExceptionSupport|FALSE
  gArmTokenSpaceGuid.PcdCpuDxeProduceDebugSupport|FALSE

[PcdsFixedAtBuild.common]
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0xFFFF0000
  gArmTokenSpaceGuid.PcdVFPEnabled|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVendor|L"CTR UEFI"

[Components]
  CtrPkg/Sec/CtrSec.inf
  MdeModulePkg/Core/Dxe/DxeMain.inf
  ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  CtrPkg/Drivers/CtrTimer/CtrTimer.inf
  CtrPkg/Drivers/CtrSd/CtrSd.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/Variable/EmuRuntimeDxe/EmuVariableRuntimeDxe.inf
  EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  EmbeddedPkg/ResetRuntimeDxe/ResetRuntimeDxe.inf
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  CtrPkg/Drivers/CtrBds/CtrBds.inf
  CtrPkg/Drivers/CtrConsole/CtrConsole.inf
  CtrPkg/Application/CtrDiagnostics/CtrDiagnostics.inf
