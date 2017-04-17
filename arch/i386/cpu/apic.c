/*
 * arch/i386/cpu/apic.c
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

#include <acpi/acpi.h>
#include <acpi/tables/madt.h>

#include <radix/asm/msr.h>
#include <radix/error.h>
#include <radix/kernel.h>
#include <radix/mm.h>
#include <radix/page.h>

#include "apic.h"

#define APIC_BASE_MSR           0x1B
#define APIC_BASE_MSR_ENABLE    0x800

static struct acpi_madt *madt;

/*
 * apic_madt_check:
 * Check that the MADT ACPI table exists and is valid, and store pointer to it.
 */
int apic_madt_check(void)
{
	madt = acpi_find_table(ACPI_MADT_SIGNATURE);
	if (!madt)
		return EINVAL;

	if (!acpi_valid_checksum((struct acpi_sdt_header *)madt)) {
		BOOT_FAIL_MSG("ACPI MADT checksum invalid\n");
		return EINVAL;
	}

	return 0;
}

static addr_t apic_get_phys_base(void)
{
	uint32_t eax, edx;

	rdmsr(APIC_BASE_MSR, &eax, &edx);
	return eax & PAGE_MASK;
}

static void apic_set_phys_base(addr_t base)
{
	wrmsr(APIC_BASE_MSR, (base & PAGE_MASK) | APIC_BASE_MSR_ENABLE, 0);
}

static uint32_t apic_reg_read(unsigned int reg)
{
	return *(uint32_t *)(__ARCH_APIC_VIRT_PAGE + (reg << 4));
}

static void apic_reg_write(unsigned int reg, int32_t value)
{
	*(uint32_t *)(__ARCH_APIC_VIRT_PAGE + (reg << 4)) = value;
}

/*
 * apic_init:
 * Configure the LAPIC to send interrupts and enable it.
 */
void apic_init(void)
{
	addr_t phys;

	phys = apic_get_phys_base();
	map_page(__ARCH_APIC_VIRT_PAGE, phys);
	apic_set_phys_base(phys);

	apic_reg_write(0xF0, apic_reg_read(0xF0) | 0x100);
}
