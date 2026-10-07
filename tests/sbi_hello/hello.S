/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * S mode payload for OpenSBI: prints a line through the SBI debug console (DBCN) and then
 * the legacy console, reads the time CSR and the misa-independent hart id, and loops.
 */
	.section .text.start
	.globl _start
_start:
	mv	s0, a0			/* hart id */
	mv	s1, a1			/* device tree */
	la	sp, stack_top

	la	a0, msg
	call	puts

	/* hart id and fdt address */
	la	a0, m_hart
	call	puts
	mv	a0, s0
	call	puthex
	la	a0, m_fdt
	call	puts
	mv	a0, s1
	call	puthex
	la	a0, m_time
	call	puts
	/* cycles and time over 200000 iterations of a two instruction loop */
	rdcycle	s2
	rdtime	s3
	li	t0, 200000
1:	addi	t0, t0, -1
	bnez	t0, 1b
	rdcycle	s4
	rdtime	s5
	la	a0, m_cyc
	call	puts
	sub	a0, s4, s2
	call	puthex
	la	a0, m_time
	call	puts
	sub	a0, s5, s3
	call	puthex
	la	a0, m_nl
	call	puts
	/* the same with a loop of loads from a small buffer: data cache */
	rdcycle	s2
	rdtime	s3
	li	t0, 200000
	la	t1, buf
2:	ld	t2, 0(t1)
	addi	t0, t0, -1
	bnez	t0, 2b
	rdcycle	s4
	rdtime	s5
	la	a0, m_ld
	call	puts
	sub	a0, s4, s2
	call	puthex
	la	a0, m_time
	call	puts
	sub	a0, s5, s3
	call	puthex
	la	a0, m_nl
	call	puts
	la	a0, m_done
	call	puts
9:	wfi
	j	9b

/* a0 = zero terminated string, DBCN write byte for each (extension 0x4442434E, function 2) */
puts:
	mv	t1, a0
3:	lbu	a0, 0(t1)
	beqz	a0, 4f
	li	a7, 0x4442434E
	li	a6, 2
	ecall
	addi	t1, t1, 1
	j	3b
4:	ret

puthex:
	mv	t2, a0
	li	t3, 60
5:	srl	a0, t2, t3
	andi	a0, a0, 15
	li	t4, 10
	blt	a0, t4, 6f
	addi	a0, a0, 'a' - 10
	j	7f
6:	addi	a0, a0, '0'
7:	li	a7, 0x4442434E
	li	a6, 2
	ecall
	addi	t3, t3, -4
	bgez	t3, 5b
	ret

	.section .rodata
msg:	.asciz "\r\nS-mode payload started by OpenSBI\r\n"
m_hart:	.asciz "hart "
m_fdt:	.asciz " fdt "
m_time:	.asciz " time "
m_cyc:	.asciz "loop cycles "
m_ld:	.asciz "load loop cycles "
m_nl:	.asciz "\r\n"
m_done:	.asciz "payload done\r\n"

	.section .bss
	.align 4
	.space 4096
buf:	.space 64
stack_top:
