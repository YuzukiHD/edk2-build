/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The few C library functions the display stack calls, and the compiler support functions
 * the target library does not have. There is no C library in the firmware.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "libc_shim.h"

void *memcpy(void *d, const void *s, size_t n)
{
	uint8_t *dp = d;
	const uint8_t *sp = s;

	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	uint8_t *dp = d;

	while (n--)
		*dp++ = (uint8_t)c;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *ap = a, *bp = b;

	for (; n; n--, ap++, bp++)
		if (*ap != *bp)
			return *ap < *bp ? -1 : 1;
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (uint8_t)*a - (uint8_t)*b;
}

/* count trailing zeros / parity of a 64 bit value: the compiler calls these on a core
 * without the bit manipulation extension */
int __ctzdi2(uint64_t x)
{
	int n = 0;

	if (!x)
		return 64;
	while (!(x & 1)) {
		x >>= 1;
		n++;
	}
	return n;
}

int __paritydi2(uint64_t x)
{
	x ^= x >> 32;
	x ^= x >> 16;
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return (int)(x & 1);
}

/* ------------------------------------------------------------------ */
/* vsnprintf: %s %c %d %i %u %x %X %o %p %%, flags - 0, width, precision of strings, h hh l ll z */
/* ------------------------------------------------------------------ */
struct out {
	char *buf;
	size_t size;
	size_t len;
};

static void put(struct out *o, char c)
{
	if (o->len + 1 < o->size)
		o->buf[o->len] = c;
	o->len++;
}

static void put_num(struct out *o, uint64_t v, unsigned base, int upper, int neg, int width, int zero, int left)
{
	char tmp[24];
	int n = 0, i;
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	do {
		tmp[n++] = digits[v % base];
		v /= base;
	} while (v);
	if (neg)
		tmp[n++] = '-';
	if (!left)
		for (i = n; i < width; i++)
			put(o, zero && !neg ? '0' : ' ');
	if (neg && zero && !left) {
		/* the sign goes before the zeros */
	}
	while (n)
		put(o, tmp[--n]);
	if (left)
		for (i = n; i < width; i++)
			put(o, ' ');
}

int f101_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct out o = {buf, size, 0};

	for (; *fmt; fmt++) {
		int left = 0, zero = 0, width = 0, prec = -1, lng = 0;

		if (*fmt != '%') {
			put(&o, *fmt);
			continue;
		}
		fmt++;
		for (;; fmt++) {
			if (*fmt == '-')
				left = 1;
			else if (*fmt == '0')
				zero = 1;
			else
				break;
		}
		if (*fmt == '*') {
			width = va_arg(ap, int);
			fmt++;
		} else {
			while (*fmt >= '0' && *fmt <= '9')
				width = width * 10 + (*fmt++ - '0');
		}
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			} else {
				while (*fmt >= '0' && *fmt <= '9')
					prec = prec * 10 + (*fmt++ - '0');
			}
		}
		while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h' || *fmt == 't' || *fmt == 'j') {
			if (*fmt == 'l' || *fmt == 'z' || *fmt == 't' || *fmt == 'j')
				lng++;
			fmt++;
		}
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			int n = 0, i;

			if (!s)
				s = "(null)";
			while (s[n] && (prec < 0 || n < prec))
				n++;
			if (!left)
				for (i = n; i < width; i++)
					put(&o, ' ');
			for (i = 0; i < n; i++)
				put(&o, s[i]);
			if (left)
				for (i = n; i < width; i++)
					put(&o, ' ');
			break;
		}
		case 'c':
			put(&o, (char)va_arg(ap, int));
			break;
		case 'd':
		case 'i': {
			int64_t v = lng ? va_arg(ap, long) : va_arg(ap, int);

			put_num(&o, v < 0 ? (uint64_t)-v : (uint64_t)v, 10, 0, v < 0, width, zero, left);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o': {
			uint64_t v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
			unsigned base = *fmt == 'u' ? 10 : (*fmt == 'o' ? 8 : 16);

			put_num(&o, v, base, *fmt == 'X', 0, width, zero, left);
			break;
		}
		case 'p':
			put(&o, '0');
			put(&o, 'x');
			put_num(&o, (uint64_t)(uintptr_t)va_arg(ap, void *), 16, 0, 0, 0, 0, 0);
			break;
		case '%':
			put(&o, '%');
			break;
		case '\0':
			fmt--;
			break;
		default:
			put(&o, '%');
			put(&o, *fmt);
			break;
		}
	}
	if (size)
		buf[o.len < size ? o.len : size - 1] = '\0';
	return (int)o.len;
}

int f101_snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = f101_vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = f101_vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}
