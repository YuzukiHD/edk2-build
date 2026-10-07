## @file
#  EDK II platform for the Allwinner F101 (XuanTie C907 in RV64 mode), run by OpenSBI.
#
#  S-mode payload without PEI, derived from OvmfPkg/RiscVVirt: the SEC code takes the memory
#  and the device tree from OpenSBI, the DXE core runs from the image that is loaded to the
#  start of the PSRAM. The QEMU parts of that platform (fw_cfg, virtio, PCI, flash, ramfb)
#  are not used. The console is the SBI debug console; UEFI variables live in memory.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = F101
  PLATFORM_GUID                  = 8c2b6f4e-3a57-4a0e-9d1c-5f101e0b0a01
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x0001001c
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = RISCV64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = F101Pkg/F101.fdf

  DEFINE SECURE_BOOT_ENABLE      = FALSE
  DEFINE TPM2_ENABLE             = FALSE
  DEFINE TPM2_CONFIG_ENABLE      = FALSE
  DEFINE QEMU_PV_VARS            = FALSE
  DEFINE CAPSULE_ENABLE          = FALSE
  DEFINE NETWORK_TLS_ENABLE      = FALSE
  DEFINE DEBUG_ON_SERIAL_PORT    = TRUE
  DEFINE BUILD_SHELL             = TRUE
  # the SEC code waits for a debugger (set mDebugWait to 0)
  DEFINE F101_DEBUG_WAIT         = FALSE
  # experiment: the display engine does not scan out
  DEFINE F101_NO_SCANOUT         = FALSE
  DEFINE F101_FLAGS              = -DF101_BUILD
!if $(F101_DEBUG_WAIT) == TRUE
  DEFINE F101_FLAGS              = $(F101_FLAGS) -DF101_DEBUG_WAIT
!endif
!if $(F101_NO_SCANOUT) == TRUE
  DEFINE F101_FLAGS              = $(F101_FLAGS) -DF101_NO_SCANOUT
!endif

!include MdePkg/MdeLibs.dsc.inc

[BuildOptions]
  GCC:RELEASE_*_*_CC_FLAGS       = -DMDEPKG_NDEBUG
  GCC:*_*_*_CC_FLAGS             = $(F101_FLAGS)

[BuildOptions.common.EDKII.DXE_RUNTIME_DRIVER]
  GCC: *_*_*_DLINK_FLAGS = -z common-page-size=0x1000

!include OvmfPkg/RiscVVirt/RiscVVirt.dsc.inc

[LibraryClasses.common]
  SerialPortLib|MdePkg/Library/BaseSerialPortLibRiscVSbiLib/BaseSerialPortLibRiscVSbiLibRam.inf
  PlatformSecLib|F101Pkg/Library/PlatformSecLib/PlatformSecLib.inf
  PlatformFvbLib|OvmfPkg/Library/PlatformFvbLibNull/PlatformFvbLibNull.inf
  PlatformBootManagerLib|ArmPkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.inf
  PlatformHookLib|MdeModulePkg/Library/BasePlatformHookLibNull/BasePlatformHookLibNull.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  FrameBufferBltLib|MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.inf
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  GptLib|MdeModulePkg/Library/GptLib/GptLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  TpmPlatformHierarchyLib|SecurityPkg/Library/PeiDxeTpmPlatformHierarchyLibNull/PeiDxeTpmPlatformHierarchyLib.inf
  DxeRasAgentClientLib|MdePkg/Library/DxeRiscvRasAgentClientLib/DxeRiscvRasAgentClientLib.inf
  DxeRiscvMpxyLib|MdePkg/Library/DxeRiscvMpxyLib/DxeRiscvMpxy.inf

[LibraryClasses.common.SEC]
  SerialPortLib|MdePkg/Library/BaseSerialPortLibRiscVSbiLib/BaseSerialPortLibRiscVSbiLib.inf
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerSecLib.inf

[LibraryClasses.common.PEI_CORE, LibraryClasses.common.PEIM]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerSecLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf

[LibraryClasses.common.DXE_CORE]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf

[LibraryClasses.common.DXE_DRIVER]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf
  ReportStatusCodeLib|MdeModulePkg/Library/DxeReportStatusCodeLib/DxeReportStatusCodeLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf

[LibraryClasses.common.UEFI_DRIVER]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf
  UefiScsiLib|MdePkg/Library/UefiScsiLib/UefiScsiLib.inf

[LibraryClasses.common.UEFI_APPLICATION]
  TimerLib|UefiCpuPkg/Library/RiscV64CpuTimerLib/RiscV64CpuTimerDxeLib.inf

[PcdsFeatureFlag.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutGopSupport|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdTurnOffUsbLegacySupport|TRUE

[PcdsFixedAtBuild.common]
  # bit 0: the cache management instructions (Zicbom) are used; the C907 has them
  gEfiMdePkgTokenSpaceGuid.PcdRiscVFeatureOverride|0xFFFFFFFFFFFFFFF1
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxVariableSize|0x2000
  gArmTokenSpaceGuid.PcdUefiShellDefaultBootEnable|TRUE
  gEfiMdePkgTokenSpaceGuid.PcdDefaultTerminalType|4
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|115200
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultDataBits|8
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultParity|1
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultStopBits|1
  gEfiMdeModulePkgTokenSpaceGuid.PcdMaxAuthVariableSize|0x2800
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"0.1"
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x21, 0xaa, 0x2c, 0x46, 0x14, 0x76, 0x03, 0x45, 0x83, 0x6e, 0x8a, 0xb6, 0xf4, 0x66, 0x23, 0x31 }
  gEfiShellPkgTokenSpaceGuid.PcdShellFileOperationSize|0x20000
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x02
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeUseMemory|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeUseSerial|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdStatusCodeMemorySize|1
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosEntryPointProvideMethod|0x2

  #
  # 16 MiB of PSRAM, OpenSBI and the device tree take the top 768 KiB: the page counts of the
  # memory type information are small.
  #
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesData|100
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiRuntimeServicesCode|60
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesCode|400
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiBootServicesData|700
  gEmbeddedTokenSpaceGuid.PcdMemoryTypeEfiLoaderCode|20

  #
  # The variable store is a part of the memory (EmuVariableFvbRuntimeDxe)
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableSize|0x00020000
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwWorkingSize|0x00001000
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwSpareSize|0x00021000
  gUefiOvmfPkgTokenSpaceGuid.PcdOvmfFlashNvStorageVariableBase|0x40000000

  #
  # The console UART of the loader (UART3 of the EVB), for the status code and the debug output
  # that do not go through the SBI.
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseMmio|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x02500c00
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialBaudRate|115200
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseHardwareFlowControl|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialClockRate|24000000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterStride|4

  #
  # The image is loaded to the start of the PSRAM; the temporary RAM of the SEC code
  # (stack and HOB list) follows it.
  #
  gUefiOvmfPkgTokenSpaceGuid.PcdOvmfFdBaseAddress|0x40000000
  gUefiOvmfPkgTokenSpaceGuid.PcdOvmfFirmwareFdSize|0x00300000

[PcdsDynamicDefault.common]
  gEfiMdePkgTokenSpaceGuid.PcdPlatformBootTimeOut|3
  gEfiMdeModulePkgTokenSpaceGuid.PcdPciDisableBusEnumeration|TRUE
  gEfiMdePkgTokenSpaceGuid.PcdPciExpressBaseAddress|0xFFFFFFFFFFFFFFFF
  gEfiMdePkgTokenSpaceGuid.PcdPciIoTranslation|0x0
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion|0x0300
  gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev|0x0
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvStoreReserved|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootDiscoveryPolicy|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableBase64|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwWorkingBase64|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwSpareBase64|0

!include OvmfPkg/Include/Dsc/OvmfDisplayPcds.dsc.inc

[PcdsPatchableInModule]
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableBase|0

[Components]
  #
  # SEC: the memory and the device tree come from OpenSBI, no PEI
  #
  UefiCpuPkg/SecCore/SecCoreNative.inf {
    <LibraryClasses>
      ExtractGuidedSectionLib|EmbeddedPkg/Library/PrePiExtractGuidedSectionLib/PrePiExtractGuidedSectionLib.inf
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
      HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
      PrePiHobListPointerLib|OvmfPkg/RiscVVirt/Library/PrePiHobListPointerLib/PrePiHobListPointerLib.inf
      MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
  }

  #
  # DXE
  #
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }

  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
      DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }

  #
  # Architectural protocols
  #
  UefiCpuPkg/CpuDxeRiscV64/CpuDxeRiscV64.inf
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  OvmfPkg/EmuVariableFvbRuntimeDxe/Fvb.inf {
    <LibraryClasses>
      # the module reads the flash header with Mmio*() without declaring IoLib
      NULL|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  }
  MdeModulePkg/Universal/FaultTolerantWriteDxe/FaultTolerantWriteDxe.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
      BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  }
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  UefiCpuPkg/CpuTimerDxeRiscV64/CpuTimerDxeRiscV64.inf
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf {
    <LibraryClasses>
      # it produces the device path protocols the protocol based instance needs
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }
  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf

  #
  # Console: serial through the SBI, and the graphics output when there is one
  #
  F101Pkg/Drivers/F101GopDxe/F101GopDxe.inf
  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf

  #
  # Disks and file systems
  #
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  #
  # Boot manager
  #
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Universal/SetupBrowserDxe/SetupBrowserDxe.inf
  MdeModulePkg/Universal/DriverHealthManagerDxe/DriverHealthManagerDxe.inf
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  MdeModulePkg/Application/UiApp/UiApp.inf
