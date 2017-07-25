/*
 * arch/i386/cpu/apic.c
 * Copyright (C) 2017 Alexei Frolov
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

#include <acpi/tables/madt.h>

#include <radix/asm/acpi.h>
#include <radix/asm/apic.h>
#include <radix/asm/mps.h>
#include <radix/asm/msr.h>

#include <radix/cpu.h>
#include <radix/cpumask.h>
#include <radix/irq.h>
#include <radix/kernel.h>
#include <radix/klog.h>
#include <radix/mm.h>
#include <radix/percpu.h>
#include <radix/slab.h>
#include <radix/smp.h>
#include <radix/spinlock.h>
#include <radix/vmm.h>

#include <rlibc/string.h>

#include "isr.h"

#ifdef CONFIG_MAX_IOAPICS
#define MAX_IOAPICS CONFIG_MAX_IOAPICS
#else
#define MAX_IOAPICS 8
#endif

static struct ioapic ioapic_list[MAX_IOAPICS];
unsigned int ioapics_available = 0;
static spinlock_t ioapic_lock = SPINLOCK_INIT;

#define IOAPIC_IOREGSEL         0
#define IOAPIC_IOWIN            4

#define IOAPIC_IOAPICID         0
#define IOAPIC_IOAPICVER        1
#define IOAPIC_IOAPICARB        2
#define IOAPIC_IOREDTBL         16
#define IOAPIC_IOREDLO(n)       (IOAPIC_IOREDTBL + (n) * 2)
#define IOAPIC_IOREDHI(n)       (IOAPIC_IOREDLO(n) + 1)

#define IOREDLO_DELMODE_MASK            0x700
#define IOREDLO_DELMODE_SHIFT           8
#define IOREDLO_DESTMODE_LOGICAL        (1 << 11)
#define IOREDLO_DELIVERY_STATUS         (1 << 12)
#define IOREDLO_POLARITY_ACTIVE_LOW     (1 << 13)
#define IOREDLO_REMOTE_IRR              (1 << 14)
#define IOREDLO_TRIGGER_MODE_LEVEL      (1 << 15)
#define IOREDLO_INTERRUPT_MASK          (1 << 16)

#define IOREDHI_DESTINATION_SHIFT       24


#define IA32_APIC_BASE_BSP    (1 << 8)  /* bootstrap processor */
#define IA32_APIC_BASE_EXTD   (1 << 10) /* X2APIC mode enable */
#define IA32_APIC_BASE_ENABLE (1 << 11) /* XAPIC global enable */

#define APIC_REG_APICID         0x02
#define APIC_REG_APICVER        0x03
#define APIC_REG_TPR            0x08
#define APIC_REG_APR            0x09
#define APIC_REG_PPR            0x0A
#define APIC_REG_EOI            0x0B
#define APIC_REG_RRD            0x0C
#define APIC_REG_LDR            0x0D
#define APIC_REG_DFR            0x0E
#define APIC_REG_SPURINT        0x0F
#define APIC_REG_ISR0           0x10
#define APIC_REG_ISR1           0x11
#define APIC_REG_ISR2           0x12
#define APIC_REG_ISR3           0x13
#define APIC_REG_ISR4           0x14
#define APIC_REG_ISR5           0x15
#define APIC_REG_ISR6           0x16
#define APIC_REG_ISR7           0x17
#define APIC_REG_TMR0           0x18
#define APIC_REG_TMR1           0x19
#define APIC_REG_TMR2           0x1A
#define APIC_REG_TMR3           0x1B
#define APIC_REG_TMR4           0x1C
#define APIC_REG_TMR5           0x1D
#define APIC_REG_TMR6           0x1E
#define APIC_REG_TMR7           0x1F
#define APIC_REG_IRR0           0x20
#define APIC_REG_IRR1           0x21
#define APIC_REG_IRR2           0x22
#define APIC_REG_IRR3           0x23
#define APIC_REG_IRR4           0x24
#define APIC_REG_IRR5           0x25
#define APIC_REG_IRR6           0x26
#define APIC_REG_IRR7           0x27
#define APIC_REG_ESR            0x28
#define APIC_REG_LVT_CMCI       0x2F
#define APIC_REG_ICR_LO         0x30
#define APIC_REG_ICR_HI         0x31
#define APIC_REG_LVT_TIMER      0x32
#define APIC_REG_LVT_THERMAL    0x33
#define APIC_REG_LVT_PERF       0x34
#define APIC_REG_LVT_LINT0      0x35
#define APIC_REG_LVT_LINT1      0x36
#define APIC_REG_LVT_ERROR      0x37
#define APIC_REG_TIMER_INITIAL  0x38
#define APIC_REG_TIMER_COUNT    0x39
#define APIC_REG_TIMER_DIVIDE   0x3E

#define APIC_LDR_ID_SHIFT       24
#define APIC_DFR_MODEL_FLAT     0xF0000000
#define APIC_DFR_MODEL_CLUSTER  0x00000000

#define APIC_MAX_FLAT_CPUS      8
#define APIC_MAX_CLUSTER_CPUS   60

/* Local APIC base addresses */
addr_t lapic_phys_base;
addr_t lapic_virt_base;

#define __ET    APIC_INT_EDGE_TRIGGER
#define __AH    APIC_INT_ACTIVE_HIGH
#define __MASK  APIC_INT_MASKED

static struct lapic_lvt lapic_lvt_default[] = {
	/* LINT0: EXTINT */
	{ 0, APIC_INT_MODE_EXTINT | __ET | __AH | __MASK },
	/* LINT1: NMI */
	{ 0, APIC_INT_MODE_NMI | __ET | __AH },
	/* timer */
	{ APIC_IRQ_TIMER, APIC_INT_MODE_FIXED | __ET | __AH | __MASK },
	/* error */
	{ APIC_IRQ_ERROR, APIC_INT_MODE_FIXED | __ET | __AH },
	/* PMC */
	{ 0, APIC_INT_MODE_NMI | __ET | __AH | __MASK },
	/* thermal */
	{ APIC_IRQ_THERMAL, APIC_INT_MODE_FIXED | __ET | __AH | __MASK },
	/* CMCI */
	{ APIC_IRQ_CMCI, APIC_INT_MODE_FIXED | __ET | __AH | __MASK }
};

static struct lapic lapic_list[MAX_CPUS];
static unsigned int cpus_available = 0;
static unsigned int cpus_online = 0;
static spinlock_t cpus_online_lock = SPINLOCK_INIT;

DEFINE_PER_CPU(struct lapic *, local_apic);


static uint32_t ioapic_reg_read(struct ioapic *ioapic, int reg)
{
	ioapic->base[IOAPIC_IOREGSEL] = reg;
	return ioapic->base[IOAPIC_IOWIN];
}

static void ioapic_reg_write(struct ioapic *ioapic, int reg, uint32_t value)
{
	ioapic->base[IOAPIC_IOREGSEL] = reg;
	ioapic->base[IOAPIC_IOWIN] = value;
}

struct ioapic *ioapic_from_id(unsigned int id)
{
	size_t i;

	for (i = 0; i < ioapics_available; ++i) {
		if (ioapic_list[i].id == id)
			return ioapic_list + i;
	}

	return NULL;
}

/*
 * ioapic_from_irq:
 * Return the I/O APIC that controls the given IRQ number.
 */
struct ioapic *ioapic_from_irq(unsigned int irq)
{
	size_t i;

	for (i = 0; i < ioapics_available; ++i) {
		if (irq >= ioapic_list[i].irq_base &&
		    irq < ioapic_list[i].irq_base + ioapic_list[i].irq_count)
			return ioapic_list + i;
	}

	return NULL;
}

struct ioapic *ioapic_add(int id, addr_t phys_addr, int irq_base)
{
	struct ioapic *ioapic;
	uint32_t irq_count;
	addr_t base;
	size_t i;

	if (ioapics_available == MAX_IOAPICS)
		return NULL;

	base = (addr_t)vmalloc(PAGE_SIZE);
	map_page_kernel(base, phys_addr, PROT_WRITE, PAGE_CP_UNCACHEABLE);

	ioapic = &ioapic_list[ioapics_available++];
	ioapic->id = id;
	ioapic->irq_base = irq_base;
	ioapic->base = (uint32_t *)base;

	irq_count = ioapic_reg_read(ioapic, IOAPIC_IOAPICVER);
	irq_count = ((irq_count >> 16) & 0xFF) + 1;
	ioapic->irq_count = irq_count;

	ioapic->pins = kmalloc(irq_count * sizeof *ioapic->pins);
	if (!ioapic->pins)
		panic("failed to allocate memory for I/O APIC %d\n", id);

	for (i = 0; i < irq_count; ++i) {
		ioapic->pins[i].irq = irq_base + i;

		/*
		 * Assume that IRQ 0 is an EXTINT, 1-15 are ISA IRQs
		 * and the rest are PCI.
		 */
		if (ioapic->pins[i].irq == 0) {
			ioapic_set_extint(ioapic, i);
		} else if (ioapic->pins[i].irq < ISA_IRQ_COUNT) {
			ioapic->pins[i].bus_type = BUS_TYPE_ISA;
			ioapic->pins[i].flags = APIC_INT_ACTIVE_HIGH |
			                        APIC_INT_EDGE_TRIGGER |
			                        APIC_INT_MASKED |
			                        APIC_INT_MODE_LOW_PRIO;
		} else {
			ioapic->pins[i].bus_type = BUS_TYPE_PCI;
			ioapic->pins[i].flags = APIC_INT_MASKED |
			                        APIC_INT_MODE_LOW_PRIO;
		}
	}

	return ioapic;
}

static int __ioapic_set_special(struct ioapic *ioapic,
                                unsigned int pin,
                                unsigned int irq,
                                unsigned int delivery)
{
	if (pin >= ioapic->irq_count)
		return EINVAL;

	ioapic->pins[pin].bus_type = BUS_TYPE_UNKNOWN;
	ioapic->pins[pin].irq = irq;
	ioapic->pins[pin].flags &= ~(APIC_INT_MASKED | APIC_INT_MODE_MASK);
	ioapic->pins[pin].flags |= APIC_INT_ACTIVE_HIGH |
	                           APIC_INT_EDGE_TRIGGER |
	                           delivery;

	return 0;
}

int ioapic_set_nmi(struct ioapic *ioapic, unsigned int pin)
{
	return __ioapic_set_special(ioapic, pin, APIC_IRQ_NMI,
	                            APIC_INT_MODE_NMI);
}

int ioapic_set_smi(struct ioapic *ioapic, unsigned int pin)
{
	return __ioapic_set_special(ioapic, pin, APIC_IRQ_SMI,
	                            APIC_INT_MODE_SMI);
}

int ioapic_set_extint(struct ioapic *ioapic, unsigned int pin)
{
	return __ioapic_set_special(ioapic, pin, APIC_IRQ_EXTINT,
	                            APIC_INT_MODE_EXTINT);
}

int ioapic_set_bus(struct ioapic *ioapic, unsigned int pin, int bus_type)
{
	if (pin >= ioapic->irq_count)
		return EINVAL;

	ioapic->pins[pin].bus_type = bus_type;
	return 0;
}

int ioapic_set_irq(struct ioapic *ioapic, unsigned int pin, int irq)
{
	if (pin >= ioapic->irq_count || irq > NUM_ISR_VECTORS - IRQ_BASE)
		return EINVAL;

	ioapic->pins[pin].irq = irq;
	return 0;
}

int ioapic_set_polarity(struct ioapic *ioapic, unsigned int pin, int polarity)
{
	if (pin >= ioapic->irq_count)
		return EINVAL;

	if (polarity == MP_INTERRUPT_POLARITY_ACTIVE_HIGH ||
	    polarity == ACPI_MADT_INTI_POLARITY_ACTIVE_HIGH)
		ioapic->pins[pin].flags |= APIC_INT_ACTIVE_HIGH;
	else if (polarity == MP_INTERRUPT_POLARITY_ACTIVE_LOW ||
		 polarity == ACPI_MADT_INTI_POLARITY_ACTIVE_LOW)
		ioapic->pins[pin].flags &= ~APIC_INT_ACTIVE_HIGH;
	else
		return EINVAL;

	return 0;
}

int ioapic_set_trigger_mode(struct ioapic *ioapic, unsigned int pin, int trig)
{
	if (pin >= ioapic->irq_count)
		return EINVAL;

	if (trig == MP_INTERRUPT_TRIGGER_MODE_EDGE ||
	    trig == ACPI_MADT_INTI_TRIGGER_MODE_EDGE)
		ioapic->pins[pin].flags |= APIC_INT_EDGE_TRIGGER;
	else if (trig == MP_INTERRUPT_TRIGGER_MODE_LEVEL ||
		 trig == ACPI_MADT_INTI_TRIGGER_MODE_LEVEL)
		ioapic->pins[pin].flags &= ~APIC_INT_EDGE_TRIGGER;
	else
		return EINVAL;

	return 0;
}

int ioapic_set_delivery_mode(struct ioapic *ioapic, unsigned int pin, int del)
{
	if (pin >= ioapic->irq_count)
		return EINVAL;

	switch (del) {
	case APIC_INT_MODE_FIXED:
	case APIC_INT_MODE_LOW_PRIO:
	case APIC_INT_MODE_SMI:
	case APIC_INT_MODE_NMI:
	case APIC_INT_MODE_INIT:
	case APIC_INT_MODE_EXTINT:
		ioapic->pins[pin].flags &= ~APIC_INT_MODE_MASK;
		ioapic->pins[pin].flags |= del;
		return 0;
	default:
		return EINVAL;
	}
}

/*
 * ioapic_mask:
 * Mask the IRQ controlled by the specified I/O APIC pin.
 */
int ioapic_mask(struct ioapic *ioapic, unsigned int pin)
{
	uint32_t low;

	if (pin >= ioapic->irq_count)
		return EINVAL;

	spin_lock_irq(&ioapic_lock);
	ioapic->pins[pin].flags |= APIC_INT_MASKED;
	low = ioapic_reg_read(ioapic, IOAPIC_IOREDLO(pin));
	low |= IOREDLO_INTERRUPT_MASK;
	ioapic_reg_write(ioapic, IOAPIC_IOREDLO(pin), low);
	spin_unlock_irq(&ioapic_lock);

	return 0;
}

/*
 * ioapic_unmask:
 * Unmask the IRQ controlled by the specified I/O APIC pin.
 */
int ioapic_unmask(struct ioapic *ioapic, unsigned int pin)
{
	uint32_t low;

	if (pin >= ioapic->irq_count)
		return EINVAL;

	spin_lock_irq(&ioapic_lock);
	ioapic->pins[pin].flags &= ~APIC_INT_MASKED;
	low = ioapic_reg_read(ioapic, IOAPIC_IOREDLO(pin));
	low &= ~IOREDLO_INTERRUPT_MASK;
	ioapic_reg_write(ioapic, IOAPIC_IOREDLO(pin), low);
	spin_unlock_irq(&ioapic_lock);

	return 0;
}

static void __ioapic_program_pin(struct ioapic *ioapic, unsigned int pin)
{
	struct ioapic_pin *p;
	uint32_t low, high;

	if (pin >= ioapic->irq_count)
		return;

	p = &ioapic->pins[pin];
	if (p->bus_type == BUS_TYPE_NONE)
		return;

	low = (p->irq + IRQ_BASE) | IOREDLO_DESTMODE_LOGICAL;
	low |= (p->flags & APIC_INT_MODE_MASK) << IOREDLO_DELMODE_SHIFT;

	if (!(p->flags & APIC_INT_ACTIVE_HIGH))
		low |= IOREDLO_POLARITY_ACTIVE_LOW;
	if (!(p->flags & APIC_INT_EDGE_TRIGGER))
		low |= IOREDLO_TRIGGER_MODE_LEVEL;
	if (p->flags & APIC_INT_MASKED)
		low |= IOREDLO_INTERRUPT_MASK;

	/* send interrupt to all CPUs */
	high = 0xFF << IOREDHI_DESTINATION_SHIFT;

	ioapic_reg_write(ioapic, IOAPIC_IOREDLO(pin), low);
	ioapic_reg_write(ioapic, IOAPIC_IOREDHI(pin), high);
}

/*
 * ioapic_progam_pin:
 * Program the I/O APIC redirection table entry for the specified pin
 * with data from its ioapic_pin struct.
 */
void ioapic_program_pin(struct ioapic *ioapic, unsigned int pin)
{
	spin_lock_irq(&ioapic_lock);
	__ioapic_program_pin(ioapic, pin);
	spin_unlock_irq(&ioapic_lock);
}

/*
 * ioapic_program:
 * Program all redirection table entries for the specified I/O APIC.
 */
void ioapic_program(struct ioapic *ioapic)
{
	size_t pin;

	spin_lock_irq(&ioapic_lock);
	for (pin = 0; pin < ioapic->irq_count; ++pin)
		__ioapic_program_pin(ioapic, pin);
	spin_unlock_irq(&ioapic_lock);
}

static void ioapic_program_all(void)
{
	size_t i;

	for (i = 0; i < ioapics_available; ++i)
		ioapic_program(&ioapic_list[i]);
}

static void lapic_enable(addr_t base)
{
	wrmsr(IA32_APIC_BASE, (base & PAGE_MASK) | IA32_APIC_BASE_ENABLE, 0);
}

static __always_inline uint32_t lapic_reg_read(uint16_t reg)
{
	return *(uint32_t *)(lapic_virt_base + (reg << 4));
}

static __always_inline void lapic_reg_write(uint16_t reg, uint32_t value)
{
	*(uint32_t *)(lapic_virt_base + (reg << 4)) = value;
}

struct lapic *lapic_from_id(unsigned int id)
{
	size_t i;

	for (i = 0; i < cpus_available; ++i) {
		if (lapic_list[i].id == id)
			return lapic_list + i;
	}

	return NULL;
}

struct lapic *lapic_add(unsigned int id)
{
	struct lapic *lapic;

	if (cpus_available == MAX_CPUS)
		return NULL;

	lapic = &lapic_list[cpus_available++];
	lapic->id = id;
	lapic->timer_mode = LAPIC_TIMER_UNDEFINED;
	lapic->timer_div = 1;
	memcpy(&lapic->lvts, &lapic_lvt_default, sizeof lapic->lvts);

	return lapic;
}

static __always_inline void __lvt_set_flags(struct lapic *lapic, int pin,
                                            uint32_t clear, uint32_t set)
{
	lapic->lvts[pin].flags &= ~clear;
	lapic->lvts[pin].flags |= set;
}

static int __lvt_set(uint32_t apic_id, unsigned int pin,
                     uint32_t clear, uint32_t set)
{
	struct lapic *lapic;
	size_t i;

	if (pin > APIC_LVT_MAX)
		return EINVAL;

	if (apic_id == APIC_ID_ALL) {
		for (i = 0; i < cpus_available; ++i)
			__lvt_set_flags(&lapic_list[i], pin, clear, set);
	} else {
		lapic = lapic_from_id(apic_id);
		if (!lapic)
			return EINVAL;
		__lvt_set_flags(lapic, pin, clear, set);
	}

	return 0;
}

int lapic_set_lvt_mode(uint32_t apic_id, unsigned int pin, uint32_t mode)
{
	switch (mode) {
	case APIC_INT_MODE_FIXED:
	case APIC_INT_MODE_SMI:
	case APIC_INT_MODE_NMI:
	case APIC_INT_MODE_INIT:
	case APIC_INT_MODE_EXTINT:
		return __lvt_set(apic_id, pin, APIC_INT_MODE_MASK, mode);
	default:
		return EINVAL;
	}
}

int lapic_set_lvt_polarity(uint32_t apic_id, unsigned int pin, int polarity)
{
	if (polarity == MP_INTERRUPT_POLARITY_ACTIVE_HIGH ||
	    polarity == ACPI_MADT_INTI_POLARITY_ACTIVE_HIGH)
		return __lvt_set(apic_id, pin, 0, APIC_INT_ACTIVE_HIGH);
	else if (polarity == MP_INTERRUPT_POLARITY_ACTIVE_LOW ||
		 polarity == ACPI_MADT_INTI_POLARITY_ACTIVE_LOW)
		return __lvt_set(apic_id, pin, APIC_INT_ACTIVE_HIGH, 0);
	else
		return EINVAL;
}

int lapic_set_lvt_trigger_mode(uint32_t apic_id, unsigned int pin, int trig)
{
	if (trig == MP_INTERRUPT_TRIGGER_MODE_EDGE ||
	    trig == ACPI_MADT_INTI_TRIGGER_MODE_EDGE)
		return __lvt_set(apic_id, pin, 0, APIC_INT_EDGE_TRIGGER);
	else if (trig == MP_INTERRUPT_TRIGGER_MODE_LEVEL ||
		 trig == ACPI_MADT_INTI_TRIGGER_MODE_LEVEL)
		return __lvt_set(apic_id, pin, APIC_INT_EDGE_TRIGGER, 0);
	else
		return EINVAL;
}

/*
 * find_cpu_lapic:
 * Read the local APIC ID of the executing processor,
 * find the corresponding struct lapic, and save it.
 */
static void find_cpu_lapic(void)
{
	struct lapic *lapic;
	uint32_t eax, edx;
	uint32_t lapic_id;

	if (cpu_supports(CPUID_X2APIC)) {
		/* check if operating in X2APIC mode */
		rdmsr(IA32_APIC_BASE, &eax, &edx);
		if (eax & IA32_APIC_BASE_EXTD) {
			rdmsr(IA32_X2APIC_APICID, &lapic_id, &edx);
			goto find_lapic;
		}
	}

	lapic_id = lapic_reg_read(APIC_REG_APICID) >> 24;

find_lapic:
	lapic = lapic_from_id(lapic_id);
	this_cpu_write(local_apic, lapic);
}

static uint8_t lapic_logid_flat(int cpu_number)
{
	return 1 << cpu_number;
}

static uint8_t lapic_logid_cluster(int cpu_number)
{
	uint8_t cluster, id;

	cluster = cpu_number >> 2;
	id = cpu_number & 3;

	return (cluster << 4) | id;
}

/*
 * apic_init:
 * Configure the LAPIC to send interrupts and enable it.
 */
void lapic_init(void)
{
	int cpu_number;
	uint32_t logical_id;

	spin_lock(&cpus_online_lock);
	cpu_number = cpus_online++;
	spin_unlock(&cpus_online_lock);

	this_cpu_write(processor_id, cpu_number);
	find_cpu_lapic();
	lapic_enable(lapic_phys_base);

	if (cpus_available <= APIC_MAX_FLAT_CPUS) {
		lapic_reg_write(APIC_REG_DFR, APIC_DFR_MODEL_FLAT);
		logical_id = lapic_logid_flat(cpu_number);
	} else if (cpus_available > APIC_MAX_CLUSTER_CPUS) {
		/* TODO: give multiple CPUs same logical ID */
		logical_id = 0;
	} else {
		lapic_reg_write(APIC_REG_DFR, APIC_DFR_MODEL_CLUSTER);
		logical_id = lapic_logid_cluster(cpu_number);
	}

	lapic_reg_write(APIC_REG_TPR, 0);
	lapic_reg_write(APIC_REG_LDR, logical_id << APIC_LDR_ID_SHIFT);
	lapic_reg_write(APIC_REG_SPURINT, 0x100 | APIC_IRQ_SPURIOUS);
}

/*
 * lapic_eoi:
 * Send an end of interrupt signal to the local APIC by writing
 * to its EOI register.
 */
void lapic_eoi(int irq)
{
	lapic_reg_write(APIC_REG_EOI, irq);
}

int bsp_apic_init(void)
{
	if (!cpu_supports(CPUID_APIC | CPUID_MSR) ||
	    (acpi_parse_madt() != 0 && parse_mp_tables() != 0)) {
		this_cpu_write(local_apic, NULL);
		cpus_available = 1;
		return 1;
	}

	ioapic_program_all();
	lapic_virt_base = (addr_t)vmalloc(PAGE_SIZE);
	map_page_kernel(lapic_virt_base, lapic_phys_base,
	                PROT_WRITE, PAGE_CP_UNCACHEABLE);
	lapic_init();

	return 0;
}
