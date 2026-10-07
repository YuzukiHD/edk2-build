/** @file
  Graphics output for the F101 EVB panel (1024x600, RGB through the display engine and TCON).

  The pipeline comes from the display stack in sunxi/ (f101_display.c); this driver owns a
  32 bit B8G8R8X8 frame buffer in memory, scans it out, and implements the protocol with
  FrameBufferBltLib. The data cache is written back after every change: the display engine
  reads the memory directly.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "f101_display.h"

#define BYTES_PER_PIXEL  4

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} F101_GOP_DEVICE_PATH;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL            Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE       Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION    Info;
  FRAME_BUFFER_CONFIGURE                  *BltConfig;
  UINTN                                   BltConfigSize;
  UINT32                                  Width;
  UINT32                                  Height;
  UINTN                                   Stride;
} F101_GOP;

STATIC F101_GOP_DEVICE_PATH  mDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0x5b0a3d10, 0x6c1e, 0x4f2b, { 0x9a, 0x7e, 0x1f, 0x10, 0x1d, 0x15, 0x91, 0x01 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 } }
};

STATIC F101_GOP  mGop;

//
// The registers the display stack touches. The CPU runs with the MMU on: a range that is not in
// the memory map is not mapped, an access to it is a page fault.
//
STATIC CONST struct {
  UINT64    Base;
  UINT64    Size;
} mMmio[] = {
  { 0x02000000, 0x10000   }, // PIO, CCU, PWM_BL
  { 0x03000000, 0x10000   }, // SYSCFG, SID
  { 0x03102000, 0x1000    }, // MBUS
  { 0x05000000, 0x200000  }, // display engine
  { 0x05460000, 0x2000    }, // TCON top, TCON LCD
};

STATIC
VOID
MapRegisters (
  VOID
  )
{
  UINTN       Index;
  EFI_STATUS  Status;

  for (Index = 0; Index < sizeof (mMmio) / sizeof (mMmio[0]); Index++) {
    Status = gDS->AddMemorySpace (EfiGcdMemoryTypeMemoryMappedIo, mMmio[Index].Base, mMmio[Index].Size, EFI_MEMORY_UC);
    if (EFI_ERROR (Status) && (Status != EFI_ACCESS_DENIED)) {
      DEBUG ((DEBUG_WARN, "F101Gop: AddMemorySpace %lx: %r\n", mMmio[Index].Base, Status));
    }

    Status = gDS->SetMemorySpaceAttributes (mMmio[Index].Base, mMmio[Index].Size, EFI_MEMORY_UC);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "F101Gop: SetMemorySpaceAttributes %lx: %r\n", mMmio[Index].Base, Status));
    }
  }
}

STATIC
VOID
CleanRows (
  IN UINTN  FirstRow,
  IN UINTN  Rows
  )
{
  UINT8  *Base = (UINT8 *)(UINTN)mGop.Mode.FrameBufferBase;

  if (FirstRow >= mGop.Height) {
    return;
  }

  if (FirstRow + Rows > mGop.Height) {
    Rows = mGop.Height - FirstRow;
  }

  WriteBackDataCacheRange (Base + FirstRow * mGop.Stride, Rows * mGop.Stride);
}

STATIC
EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if ((Info == NULL) || (SizeOfInfo == NULL) || (ModeNumber != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocateCopyPool (sizeof (mGop.Info), &mGop.Info);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (mGop.Info);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                        ModeNumber
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black = { 0, 0, 0, 0 };

  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }

  return This->Blt (This, &Black, EfiBltVideoFill, 0, 0, 0, 0, mGop.Width, mGop.Height, 0);
}

STATIC
EFI_STATUS
EFIAPI
GopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL        *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL       *BltBuffer OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION   BltOperation,
  IN UINTN                               SourceX,
  IN UINTN                               SourceY,
  IN UINTN                               DestinationX,
  IN UINTN                               DestinationY,
  IN UINTN                               Width,
  IN UINTN                               Height,
  IN UINTN                               Delta
  )
{
  EFI_STATUS    Status;
  Status = FrameBufferBlt (
             mGop.BltConfig,
             BltBuffer,
             BltOperation,
             SourceX,
             SourceY,
             DestinationX,
             DestinationY,
             Width,
             Height,
             Delta
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (BltOperation) {
    case EfiBltVideoFill:
    case EfiBltBufferToVideo:
      CleanRows (DestinationY, Height);
      break;
    case EfiBltVideoToVideo:
      CleanRows (DestinationY, Height);
      break;
    default:
      break;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
F101GopEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  UINT32                Width, Height, Hz;
  UINTN                 Size;
  EFI_PHYSICAL_ADDRESS  Fb;
  EFI_HANDLE            Handle;

  MapRegisters ();

  if (f101_display_init (&Width, &Height, &Hz) != 0) {
    DEBUG ((DEBUG_ERROR, "F101Gop: the display pipeline did not come up\n"));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "F101Gop: panel %ux%u @ %u Hz\n", Width, Height, Hz));

  mGop.Width  = Width;
  mGop.Height = Height;
  mGop.Stride = (UINTN)Width * BYTES_PER_PIXEL;
  Size        = ALIGN_VALUE (mGop.Stride * Height, EFI_PAGE_SIZE);

  Fb     = 0;
  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (Size), &Fb);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "F101Gop: no memory for the frame buffer: %r\n", Status));
    return Status;
  }

  ZeroMem ((VOID *)(UINTN)Fb, Size);
  DEBUG ((DEBUG_INFO, "F101Gop: frame buffer %lx - %lx (%u pages)\n", Fb, Fb + Size - 1, (UINT32)EFI_SIZE_TO_PAGES (Size)));

  mGop.Info.Version              = 0;
  mGop.Info.HorizontalResolution = Width;
  mGop.Info.VerticalResolution   = Height;
  mGop.Info.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mGop.Info.PixelsPerScanLine    = Width;

  mGop.Mode.MaxMode         = 1;
  mGop.Mode.Mode            = 0;
  mGop.Mode.Info            = &mGop.Info;
  mGop.Mode.SizeOfInfo      = sizeof (mGop.Info);
  mGop.Mode.FrameBufferBase = Fb;
  mGop.Mode.FrameBufferSize = Size;

  mGop.Gop.QueryMode = GopQueryMode;
  mGop.Gop.SetMode   = GopSetMode;
  mGop.Gop.Blt       = GopBlt;
  mGop.Gop.Mode      = &mGop.Mode;

  mGop.BltConfigSize = 0;
  Status             = FrameBufferBltConfigure ((VOID *)(UINTN)Fb, &mGop.Info, mGop.BltConfig, &mGop.BltConfigSize);
  if (Status == RETURN_BUFFER_TOO_SMALL) {
    mGop.BltConfig = AllocatePool (mGop.BltConfigSize);
    if (mGop.BltConfig == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = FrameBufferBltConfigure ((VOID *)(UINTN)Fb, &mGop.Info, mGop.BltConfig, &mGop.BltConfigSize);
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "F101Gop: FrameBufferBltConfigure: %r\n", Status));
    return Status;
  }

  WriteBackDataCacheRange ((VOID *)(UINTN)Fb, Size);
 #ifdef F101_NO_SCANOUT
  DEBUG ((DEBUG_INFO, "F101Gop: scan out disabled (experiment)\n"));
 #else
  if (f101_display_show ((UINTN)Fb, (UINT32)mGop.Stride, Width, Height) != 0) {
    DEBUG ((DEBUG_ERROR, "F101Gop: the frame buffer was not accepted\n"));
    return EFI_DEVICE_ERROR;
  }

  f101_display_backlight (255);
 #endif

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mDevicePath,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop.Gop,
                  NULL
                  );
  DEBUG ((DEBUG_INFO, "F101Gop: graphics output installed: %r\n", Status));
  return Status;
}
