/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef LIBC_SHIM_H_
#define LIBC_SHIM_H_

#include <stdarg.h>
#include <stddef.h>

int f101_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int f101_snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* LIBC_SHIM_H_ */
