/*
 * arch/i386/include/radix/arch_irq.h
 * Copyright (C) 2016-2017 Alexei Frolov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARCH_I386_RADIX_IRQ_H
#define ARCH_I386_RADIX_IRQ_H

#include <radix/compiler.h>

#define __ARCH_SYSCALL_VECTOR   0x30

#define __ARCH_TIMER_IRQ        0x0
#define __ARCH_KBD_IRQ          0x1

#define __arch_irq_init         idt_init
#define __arch_in_irq           in_interrupt
#define __arch_irq_active       interrupts_active
#define __arch_irq_disable      interrupt_disable
#define __arch_irq_enable       interrupt_enable
#define __arch_irq_install      install_interrupt_handler
#define __arch_irq_uninstall    uninstall_interrupt_handler

#define __INTERRUPT_BIT (1 << 9)

#include <radix/sys.h>

void idt_init(void);
int in_interrupt(void);
void interrupt_disable(void);
void interrupt_enable(void);

int install_exception_handler(uint32_t intno, void (*hnd)(struct regs *, int));
int uninstall_exception_handler(uint32_t intno);
int install_interrupt_handler(uint32_t intno, void (*hnd)(struct regs *));
int uninstall_interrupt_handler(uint32_t intno);

#include <radix/types.h>

static __always_inline int interrupts_active(void)
{
	uint32_t flags;

	asm volatile("pushf;"
	             "pop %0;"
	             :"=g"(flags));
	return flags & __INTERRUPT_BIT;
}

#endif /* ARCH_I386_RADIX_IRQ_H */