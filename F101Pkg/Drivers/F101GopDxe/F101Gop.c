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
#include <Library/TimerLib.h>
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
  FRAME_BUFFER_CONFIGURE                  *BltConfig[2];
  UINTN                                   BltConfigSize;
  UINT8                                   *Buf[2];
  BOOLEAN                                 Single;      // one buffer only: drawn in place
  UINTN                                   Front;       // the buffer being scanned out
  UINTN                                   DirtyFirst;  // rows drawn into the back buffer, not shown yet
  UINTN                                   DirtyEnd;
  UINTN                                   StaleFirst;  // rows the back buffer lacks since the last flip
  UINTN                                   StaleEnd;
  UINT64                                  LastBlt;     // ns, last drawing
  UINT64                                  DirtySince;  // ns, first drawing not shown yet
  UINT64                                  FlipTime;    // ns, when the last flip was submitted
  EFI_EVENT                               PresentTimer;
  EFI_EVENT                               ExitEvent;
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

//
// Double buffering: drawing goes to the back buffer; a timer flips the display engine to it, so a
// frame is never scanned out half drawn. The buffer that was shown before lacks the rows drawn
// since, they are copied over (from the buffer on screen) before the next drawing touches it.
//
#define FLIP_LATCH_NS  20000000ULL   // a flip is taken over by the engine within one frame (17 ms)
#define PRESENT_100NS  100000ULL     // timer period 10 ms
#define IDLE_NS        8000000ULL    // flip once the drawing paused this long
#define MAX_AGE_NS     100000000ULL  // or the picture is this old

STATIC
UINT64
NowNs (
  VOID
  )
{
  return GetTimeInNanoSecond (GetPerformanceCounter ());
}

STATIC
VOID
AddRows (
  IN OUT UINTN  *First,
  IN OUT UINTN  *End,
  IN     UINTN  Y,
  IN     UINTN  Rows
  )
{
  UINTN  E;

  if (Y >= mGop.Height) {
    return;
  }

  E = MIN (Y + Rows, (UINTN)mGop.Height);
  if (*First >= *End) {
    *First = Y;
    *End   = E;
  } else {
    *First = MIN (*First, Y);
    *End   = MAX (*End, E);
  }
}

STATIC
VOID
SyncBack (
  VOID
  )
{
  UINTN  Back = 1 - mGop.Front;
  UINT64 Now;

  if (mGop.StaleFirst >= mGop.StaleEnd) {
    return;
  }

  Now = NowNs ();
  if (Now < mGop.FlipTime + FLIP_LATCH_NS) {
    MicroSecondDelay ((UINTN)((mGop.FlipTime + FLIP_LATCH_NS - Now) / 1000));
  }

  CopyMem (
    mGop.Buf[Back] + mGop.StaleFirst * mGop.Stride,
    mGop.Buf[mGop.Front] + mGop.StaleFirst * mGop.Stride,
    (mGop.StaleEnd - mGop.StaleFirst) * mGop.Stride
    );
  mGop.StaleFirst = mGop.StaleEnd = 0;
}

STATIC
VOID
Present (
  VOID
  )
{
  UINTN  Back = 1 - mGop.Front;

  if (mGop.DirtyFirst >= mGop.DirtyEnd) {
    return;
  }

  SyncBack ();
  WriteBackDataCacheRange (
    mGop.Buf[Back] + mGop.DirtyFirst * mGop.Stride,
    (mGop.DirtyEnd - mGop.DirtyFirst) * mGop.Stride
    );
 #ifndef F101_NO_SCANOUT
  f101_display_show ((UINTN)mGop.Buf[Back], (UINT32)mGop.Stride, mGop.Width, mGop.Height);
 #endif
  mGop.FlipTime    = NowNs ();
  mGop.Front       = Back;
  mGop.StaleFirst  = mGop.DirtyFirst;
  mGop.StaleEnd    = mGop.DirtyEnd;
  mGop.DirtyFirst  = mGop.DirtyEnd = 0;
}

STATIC
VOID
EFIAPI
PresentTimerNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINT64  Now = NowNs ();

  //
  // Let a burst of drawing finish (every drawing right after a flip waits for the engine to
  // take it over); show the picture when it pauses or has waited long enough.
  //
  if ((Now - mGop.LastBlt >= IDLE_NS) || (Now - mGop.DirtySince >= MAX_AGE_NS)) {
    Present ();
  }
}

//
// The operating system draws into Mode.FrameBufferBase (buffer 0) itself: show the last picture
// there and stop flipping.
//
STATIC
VOID
EFIAPI
ExitBootServicesNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->SetTimer (mGop.PresentTimer, TimerCancel, 0);
  Present ();
  if (mGop.Front != 0) {
    CopyMem (mGop.Buf[0], mGop.Buf[1], mGop.Stride * mGop.Height);
    WriteBackDataCacheRange (mGop.Buf[0], mGop.Stride * mGop.Height);
    f101_display_show ((UINTN)mGop.Buf[0], (UINT32)mGop.Stride, mGop.Width, mGop.Height);
    mGop.Front = 0;
  }
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
  EFI_STATUS  Status;
  EFI_TPL     OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  gBS->RestoreTPL (OldTpl);
  if (OldTpl < TPL_CALLBACK) {
    OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  }

  if (mGop.Single) {
    Status = FrameBufferBlt (mGop.BltConfig[0], BltBuffer, BltOperation, SourceX, SourceY, DestinationX, DestinationY, Width, Height, Delta);
    if (!EFI_ERROR (Status) && (BltOperation != EfiBltVideoToBltBuffer) && (DestinationY < mGop.Height)) {
      WriteBackDataCacheRange (mGop.Buf[0] + DestinationY * mGop.Stride, MIN (Height, mGop.Height - DestinationY) * mGop.Stride);
    }

    gBS->RestoreTPL (OldTpl);
    return Status;
  }

  SyncBack ();

  Status = FrameBufferBlt (
             mGop.BltConfig[1 - mGop.Front],
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
  if (!EFI_ERROR (Status) && (BltOperation != EfiBltVideoToBltBuffer)) {
    if (mGop.DirtyFirst >= mGop.DirtyEnd) {
      mGop.DirtySince = NowNs ();
    }

    AddRows (&mGop.DirtyFirst, &mGop.DirtyEnd, DestinationY, Height);
    mGop.LastBlt = NowNs ();
  }

  gBS->RestoreTPL (OldTpl);
  return Status;
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
  EFI_PHYSICAL_ADDRESS  Fb, Fb2;
  EFI_HANDLE            Handle;
  UINTN                 Index;

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
  mGop.Buf[0] = (UINT8 *)(UINTN)Fb;
  mGop.Front  = 0;

  Fb2    = 0;
  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (Size), &Fb2);
  if (EFI_ERROR (Status)) {
    //
    // Not enough memory for a second buffer: draw into the one on screen.
    //
    DEBUG ((DEBUG_WARN, "F101Gop: no back buffer (%r), single buffered\n", Status));
    mGop.Buf[1] = mGop.Buf[0];
    mGop.Single = TRUE;
  } else {
    ZeroMem ((VOID *)(UINTN)Fb2, Size);
    mGop.Buf[1] = (UINT8 *)(UINTN)Fb2;
  }
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

  for (Index = 0; Index < 2; Index++) {
    mGop.BltConfigSize = 0;
    Status             = FrameBufferBltConfigure (mGop.Buf[Index], &mGop.Info, mGop.BltConfig[Index], &mGop.BltConfigSize);
    if (Status == RETURN_BUFFER_TOO_SMALL) {
      mGop.BltConfig[Index] = AllocatePool (mGop.BltConfigSize);
      if (mGop.BltConfig[Index] == NULL) {
        return EFI_OUT_OF_RESOURCES;
      }

      Status = FrameBufferBltConfigure (mGop.Buf[Index], &mGop.Info, mGop.BltConfig[Index], &mGop.BltConfigSize);
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "F101Gop: FrameBufferBltConfigure: %r\n", Status));
      return Status;
    }
  }

  WriteBackDataCacheRange (mGop.Buf[0], Size);
  WriteBackDataCacheRange (mGop.Buf[1], Size);
 #ifndef F101_NO_SCANOUT
  if (f101_display_show ((UINTN)mGop.Buf[0], (UINT32)mGop.Stride, Width, Height) != 0) {
    DEBUG ((DEBUG_ERROR, "F101Gop: the frame buffer was not accepted\n"));
    return EFI_DEVICE_ERROR;
  }

  f101_display_backlight (255);
 #endif

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, PresentTimerNotify, NULL, &mGop.PresentTimer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mGop.PresentTimer, TimerPeriodic, PRESENT_100NS);
  }

  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, ExitBootServicesNotify, NULL, &mGop.ExitEvent);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

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
