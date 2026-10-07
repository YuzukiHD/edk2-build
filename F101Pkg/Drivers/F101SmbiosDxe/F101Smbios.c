/** @file
  SMBIOS tables of the board: the boot menu reads the computer model (type 1), the processor
  (type 4) and the memory size (type 19) from them.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Protocol/Smbios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define RAM_BASE        0x40000000ULL
#define RAM_SIZE        0x01000000ULL
#define CPU_CURRENT_MHZ 600
#define CPU_MAX_MHZ     1008

STATIC
VOID
AddRecord (
  IN EFI_SMBIOS_PROTOCOL  *Smbios,
  IN VOID                 *Fixed,
  IN UINTN                FixedSize,
  IN CONST CHAR8          **Strings
  )
{
  EFI_SMBIOS_HANDLE  Handle;
  EFI_STATUS         Status;
  UINTN              Total = FixedSize;
  UINTN              Index;
  UINT8              *Buffer;
  UINT8              *Cursor;

  for (Index = 0; Strings[Index] != NULL; Index++) {
    Total += AsciiStrLen (Strings[Index]) + 1;
  }

  Total += (Index == 0) ? 2 : 1;      // the string set ends with an empty string

  Buffer = AllocateZeroPool (Total);
  if (Buffer == NULL) {
    return;
  }

  CopyMem (Buffer, Fixed, FixedSize);
  Cursor = Buffer + FixedSize;
  for (Index = 0; Strings[Index] != NULL; Index++) {
    UINTN  Length = AsciiStrLen (Strings[Index]) + 1;
    CopyMem (Cursor, Strings[Index], Length);
    Cursor += Length;
  }

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Status = Smbios->Add (Smbios, NULL, &Handle, (EFI_SMBIOS_TABLE_HEADER *)Buffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "F101Smbios: add type %u: %r\n", ((EFI_SMBIOS_TABLE_HEADER *)Fixed)->Type, Status));
  }

  FreePool (Buffer);
}

STATIC
VOID
AsciiFromUnicode (
  IN  CONST CHAR16  *Source,
  OUT CHAR8         *Dest,
  IN  UINTN         Size
  )
{
  UnicodeStrToAsciiStrS (Source, Dest, Size);
}

EFI_STATUS
EFIAPI
F101SmbiosEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_SMBIOS_PROTOCOL   *Smbios;
  CHAR8                 Vendor[32];
  CHAR8                 Version[48];

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  AsciiFromUnicode ((CHAR16 *)PcdGetPtr (PcdFirmwareVendor), Vendor, sizeof (Vendor));
  AsciiFromUnicode ((CHAR16 *)PcdGetPtr (PcdFirmwareVersionString), Version, sizeof (Version));

  //
  // Type 0: BIOS information
  //
  {
    SMBIOS_TABLE_TYPE0  T = { { EFI_SMBIOS_TYPE_BIOS_INFORMATION, sizeof (SMBIOS_TABLE_TYPE0), 0 } };
    CONST CHAR8         *S[] = { Vendor, Version, "10/07/2026", NULL };

    T.Vendor      = 1;
    T.BiosVersion = 2;
    T.BiosReleaseDate = 3;
    T.BiosSize    = 0;
    T.BiosCharacteristics.BiosCharacteristicsNotSupported = 1;
    T.SystemBiosMajorRelease = 0;
    T.SystemBiosMinorRelease = 1;
    T.EmbeddedControllerFirmwareMajorRelease = 0xFF;
    T.EmbeddedControllerFirmwareMinorRelease = 0xFF;
    AddRecord (Smbios, &T, sizeof (T), S);
  }

  //
  // Type 1: system information, the computer model of the menu
  //
  {
    SMBIOS_TABLE_TYPE1  T = { { EFI_SMBIOS_TYPE_SYSTEM_INFORMATION, sizeof (SMBIOS_TABLE_TYPE1), 0 } };
    CONST CHAR8         *S[] = { "Allwinner", "Allwinner F101 EVB", "1.0", NULL };

    T.Manufacturer = 1;
    T.ProductName  = 2;
    T.Version      = 3;
    T.WakeUpType   = SystemWakeupTypeUnknown;
    AddRecord (Smbios, &T, sizeof (T), S);
  }

  //
  // Type 4: processor
  //
  {
    SMBIOS_TABLE_TYPE4  T = { { EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION, sizeof (SMBIOS_TABLE_TYPE4), 0 } };
    CONST CHAR8         *S[] = { "CPU0", "Allwinner", "XuanTie C907 (RV64GCV)", NULL };

    T.Socket           = 1;
    T.ProcessorType    = CentralProcessor;
    T.ProcessorFamily  = ProcessorFamilyIndicatorFamily2;
    T.ProcessorManufacturer = 2;
    T.ProcessorVersion = 3;
    T.Voltage.ProcessorVoltageIndicateLegacy = 0;
    T.MaxSpeed         = CPU_MAX_MHZ;
    T.CurrentSpeed     = CPU_CURRENT_MHZ;
    T.Status           = 0x41;               // populated, enabled
    T.ProcessorUpgrade = ProcessorUpgradeUnknown;
    T.CoreCount        = 1;
    T.EnabledCoreCount = 1;
    T.ThreadCount      = 1;
    T.ProcessorFamily2 = ProcessorFamilyRiscVRV64;
    AddRecord (Smbios, &T, sizeof (T), S);
  }

  //
  // Type 19: memory array mapped address
  //
  {
    SMBIOS_TABLE_TYPE19  T = { { EFI_SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS, sizeof (SMBIOS_TABLE_TYPE19), 0 } };
    CONST CHAR8          *S[] = { NULL };

    T.StartingAddress = (UINT32)(RAM_BASE >> 10);
    T.EndingAddress   = (UINT32)((RAM_BASE + RAM_SIZE - 1) >> 10);
    T.PartitionWidth  = 1;
    AddRecord (Smbios, &T, sizeof (T), S);
  }

  return EFI_SUCCESS;
}
