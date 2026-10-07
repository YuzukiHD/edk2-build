/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * What the display stack needs from the firmware, as plain C: the stack is written against
 * the C library headers, which do not mix with the EDK II ones in one source file.
 */
#ifndef EDK_GLUE_H_
#define EDK_GLUE_H_

#include <stddef.h>
#include <stdint.h>

void *edk_alloc_zero(size_t size);
void edk_free(void *ptr);
/* page aligned, zeroed memory that never moves (frame buffers, descriptor lists) */
void *edk_alloc_pages(size_t size);
void edk_free_pages(void *ptr, size_t size);

void edk_dcache_clean(const void *ptr, size_t size);
void edk_dcache_invalidate(void *ptr, size_t size);

void edk_udelay(uint32_t us);
uint64_t edk_time_us(void);

/* the vertical blank interrupt of the display engine is polled: a periodic timer event */
typedef void (*edk_poll_fn)(void *data);
int edk_poll_start(edk_poll_fn fn, void *data, uint32_t period_us, void **handle);
void edk_poll_stop(void *handle);

/* run a call at a raised task priority level (the interrupt emulation runs there) */
unsigned long edk_tpl_raise(void);
void edk_tpl_restore(unsigned long tpl);
int edk_in_poll(void);

void edk_log(int level, const char *msg);

#endif /* EDK_GLUE_H_ */
