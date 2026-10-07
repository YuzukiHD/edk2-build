/** @file
  EDK II side of the display stack OS layer: memory, cache, time, a polling timer, log.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "edk_glue.h"

#define MTIME_HZ  24000000ULL

typedef struct {
  EFI_EVENT    Event;
  edk_poll_fn  Fn;
  VOID         *Data;
} POLLER;

static UINTN  mInPoll;

void *
edk_alloc_zero (
  size_t  size
  )
{
  return AllocateZeroPool (size);
}

void
edk_free (
  void  *ptr
  )
{
  if (ptr != NULL) {
    FreePool (ptr);
  }
}

void *
edk_alloc_pages (
  size_t  size
  )
{
  VOID  *Ptr;

  Ptr = AllocateAlignedPages (EFI_SIZE_TO_PAGES (size), EFI_PAGE_SIZE);
  if (Ptr != NULL) {
    ZeroMem (Ptr, size);
  }

  return Ptr;
}

void
edk_free_pages (
  void    *ptr,
  size_t  size
  )
{
  if (ptr != NULL) {
    FreeAlignedPages (ptr, EFI_SIZE_TO_PAGES (size));
  }
}

void
edk_dcache_clean (
  const void  *ptr,
  size_t      size
  )
{
  WriteBackDataCacheRange ((VOID *)ptr, size);
}

void
edk_dcache_invalidate (
  void    *ptr,
  size_t  size
  )
{
  InvalidateDataCacheRange (ptr, size);
}

void
edk_udelay (
  uint32_t  us
  )
{
  MicroSecondDelay (us);
}

uint64_t
edk_time_us (
  void
  )
{
  UINT64  Ticks;

  __asm__ volatile ("rdtime %0" : "=r" (Ticks));
  return DivU64x32 (MultU64x32 (Ticks, 1000000), (UINT32)MTIME_HZ);
}

STATIC
VOID
EFIAPI
PollNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  POLLER  *P = Context;

  mInPoll++;
  P->Fn (P->Data);
  mInPoll--;
}

int
edk_poll_start (
  edk_poll_fn  fn,
  void         *data,
  uint32_t     period_us,
  void         **handle
  )
{
  POLLER      *P;
  EFI_STATUS  Status;

  P = AllocateZeroPool (sizeof (*P));
  if (P == NULL) {
    return -1;
  }

  P->Fn   = fn;
  P->Data = data;
  Status  = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, PollNotify, P, &P->Event);
  if (!EFI_ERROR (Status)) {
    /* the timer is in units of 100 ns */
    Status = gBS->SetTimer (P->Event, TimerPeriodic, (UINT64)period_us * 10);
  }

  if (EFI_ERROR (Status)) {
    FreePool (P);
    return -1;
  }

  *handle = P;
  return 0;
}

void
edk_poll_stop (
  void  *handle
  )
{
  POLLER  *P = handle;

  if (P != NULL) {
    gBS->SetTimer (P->Event, TimerCancel, 0);
    gBS->CloseEvent (P->Event);
    FreePool (P);
  }
}

unsigned long
edk_tpl_raise (
  void
  )
{
  return (unsigned long)gBS->RaiseTPL (TPL_NOTIFY);
}

void
edk_tpl_restore (
  unsigned long  tpl
  )
{
  gBS->RestoreTPL ((EFI_TPL)tpl);
}

int
edk_in_poll (
  void
  )
{
  return mInPoll != 0;
}

void
edk_log (
  int         level,
  const char  *msg
  )
{
  UINTN  Mask = (level == 0) ? DEBUG_ERROR : ((level == 1) ? DEBUG_WARN : DEBUG_INFO);

  DEBUG ((Mask, "display: %a\n", msg));
}
