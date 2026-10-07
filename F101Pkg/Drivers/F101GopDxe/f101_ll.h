/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/*
 * Register level access to the PIO banks, used by the OS layer of the display stack.
 */
#ifndef F101_LL_H_
#define F101_LL_H_

#include <stdint.h>

#define F101_REG32(a)		(*(volatile uint32_t *)(uintptr_t)(a))

#define F101_PIO_BASE		0x02000000u
#define F101_PIO_BANK_STRIDE	0x30u

enum { F101_GPIO_IN = 0, F101_GPIO_OUT = 1 };

static inline uint32_t f101_pio_bank(int pin)
{
	return F101_PIO_BASE + (uint32_t)(pin / 32) * F101_PIO_BANK_STRIDE;
}

/* function 0 = input, 1 = output, 2.. = peripheral mux */
static inline void f101_pin_mux(int pin, uint32_t mux)
{
	uint32_t r = f101_pio_bank(pin) + (uint32_t)((pin % 32) / 8) * 4;
	uint32_t sh = (uint32_t)(pin % 8) * 4;

	F101_REG32(r) = (F101_REG32(r) & ~(0xfu << sh)) | (mux << sh);
}

static inline void f101_pin_pull(int pin, uint32_t pull)
{
	uint32_t r = f101_pio_bank(pin) + 0x24u + (uint32_t)((pin % 32) / 16) * 4;
	uint32_t sh = (uint32_t)(pin % 16) * 2;

	F101_REG32(r) = (F101_REG32(r) & ~(0x3u << sh)) | (pull << sh);
}

static inline void f101_pin_drive(int pin, uint32_t level)
{
	uint32_t r = f101_pio_bank(pin) + 0x14u + (uint32_t)((pin % 32) / 16) * 4;
	uint32_t sh = (uint32_t)(pin % 16) * 2;

	F101_REG32(r) = (F101_REG32(r) & ~(0x3u << sh)) | ((level & 3u) << sh);
}

static inline void f101_gpio_set_level(int pin, int level)
{
	uint32_t r = f101_pio_bank(pin) + 0x10u;

	if (level)
		F101_REG32(r) |= 1u << (pin % 32);
	else
		F101_REG32(r) &= ~(1u << (pin % 32));
}

static inline int f101_gpio_get_level(int pin)
{
	return (F101_REG32(f101_pio_bank(pin) + 0x10u) >> (pin % 32)) & 1;
}

#endif /* F101_LL_H_ */
