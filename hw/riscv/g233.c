/*
 * QEMU RISC-V G233 Board (Learning QEMU 2025)
 *
 * Copyright (c) 2025 Zevorn(Chao Liu) chao.liu@yeah.net
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/typedefs.h"
#include "system/block-backend-global-state.h"
#include "system/system.h"
#include "system/memory.h"
#include "target/riscv/cpu.h"
#include "chardev/char.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/riscv/boot.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/unimp.h"
#include "hw/char/pl011.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/g233.h"
#include "hw/ssi/g233_spi.h"

static const MemMapEntry g233_memmap[] = {
	[G233_DEV_MROM] = { 0x1000, 0x2000 },	      [G233_DEV_CLINT] = { 0x2000000, 0x10000 },
	[G233_DEV_PLIC] = { 0xc000000, 0x4000000 },   [G233_DEV_UART0] = { 0x10000000, 0x1000 },
	[G233_DEV_GPIO0] = { 0x10012000, 0x1000 },    [G233_DEV_PWM0] = { 0x10015000, 0x1000 },
	[G233_DEV_DRAM] = { 0x80000000, 0x40000000 },
};

static void g233_soc_instance_init(Object *obj)
{
	G233SoCState *s = RISCV_G233_SOC(obj);

	// single core
	object_initialize_child(OBJECT(s), "g233-cpu", &s->cpus, TYPE_RISCV_HART_ARRAY);

	object_property_set_str(OBJECT(&s->cpus), "cpu-type", TYPE_RISCV_CPU_GEVICO_G233, &error_fatal);
	object_property_set_int(OBJECT(&s->cpus), "hartid-base", 0, &error_fatal);
	object_property_set_int(OBJECT(&s->cpus), "num-harts", 1, &error_fatal);
	object_property_set_int(OBJECT(&s->cpus), "resetvec", g233_memmap[G233_DEV_MROM].base + 4, &error_fatal);

	// GPIO
	object_initialize_child(obj, "sifive.gpio", &s->gpio, TYPE_SIFIVE_GPIO);
}

static void g233_soc_realize(DeviceState *dev, Error **errp)
{
	MachineState *ms = MACHINE(qdev_get_machine());
	G233SoCState *s = RISCV_G233_SOC(dev);
	MemoryRegion *sys_mem = get_system_memory();
	const MemMapEntry *memmap = g233_memmap;

	/* CPUs realize */
	if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpus), errp)) {
		return;
	}

	/* Mask ROM */
	memory_region_init_rom(&s->mask_rom, OBJECT(dev), "riscv.g233.mrom", memmap[G233_DEV_MROM].size, &error_fatal);
	memory_region_add_subregion(sys_mem, memmap[G233_DEV_MROM].base, &s->mask_rom);

	/* MMIO */
	s->plic = sifive_plic_create(memmap[G233_DEV_PLIC].base, (char *)G233_PLIC_HART_CONFIG, ms->smp.cpus, 0,
				     G233_PLIC_NUM_SOURCES, G233_PLIC_NUM_PRIORITIES, G233_PLIC_PRIORITY_BASE,
				     G233_PLIC_PENDING_BASE, G233_PLIC_ENABLE_BASE, G233_PLIC_ENABLE_STRIDE,
				     G233_PLIC_CONTEXT_BASE, G233_PLIC_CONTEXT_STRIDE, memmap[G233_DEV_PLIC].size);
	riscv_aclint_swi_create(memmap[G233_DEV_CLINT].base, 0, ms->smp.cpus, false);
	riscv_aclint_mtimer_create(memmap[G233_DEV_CLINT].base + RISCV_ACLINT_SWI_SIZE,
				   RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, ms->smp.cpus, RISCV_ACLINT_DEFAULT_MTIMECMP,
				   RISCV_ACLINT_DEFAULT_MTIME, 32768, false); /* TODO: set default freq */

	/* GPIO */
	if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
		return;
	}

	/* Map GPIO registers */
	sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[G233_DEV_GPIO0].base);

	/* Pass all GPIOs to the SOC layer so they are available to the board */
	qdev_pass_gpios(DEVICE(&s->gpio), dev, NULL);

	/* Connect GPIO interrupts to the PLIC */
	for (int i = 0; i < 32; i++) {
		sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), i, qdev_get_gpio_in(DEVICE(s->plic), G233_GPIO0_IRQ0 + i));
	}

	/* Add UART (PL011) */
	s->uart0 = pl011_create(memmap[G233_DEV_UART0].base, qdev_get_gpio_in(DEVICE(s->plic), G233_UART0_IRQ),
				serial_hd(0));

	/* SiFive.PWM0 */
	create_unimplemented_device("riscv.g233.pwm0", memmap[G233_DEV_PWM0].base + 4, memmap[G233_DEV_PWM0].size);
}

static void g233_soc_class_init(ObjectClass *oc, const void *data)
{
	DeviceClass *dc = DEVICE_CLASS(oc);

	dc->realize = g233_soc_realize;
}

static void g233_machine_init(MachineState *machine)
{
	MachineClass *mc = MACHINE_GET_CLASS(machine);
	const MemMapEntry *memmap = g233_memmap;

	G233MachineState *s = RISCV_G233_MACHINE(machine);
	int i;
	RISCVBootInfo boot_info;
	DeviceState *flash_dev;
	BlockBackend *blk;

	if (machine->ram_size < mc->default_ram_size) { // mc->default_ram_size = g233_memmap[G233_DEV_DRAM].size;
		char *sz = size_to_str(mc->default_ram_size);
		error_report("Invalid RAM size, should be %s", sz);
		g_free(sz);
		exit(EXIT_FAILURE);
	}

	/* Initialize SoC */
	object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RISCV_G233_SOC);
	// 不能直接调用这个g233_soc_realize(DEVICE(&s->soc), &error_fatal);
	qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

	/* Data Memory(DDR RAM) */
	memory_region_add_subregion(get_system_memory(), memmap[G233_DEV_DRAM].base, machine->ram);

	/* Mask ROM reset vector */
	uint32_t reset_vec[5];
	reset_vec[1] = 0x0010029b; /* 0x1004: addiw  t0, zero, 1 */
	reset_vec[2] = 0x01f29293; /* 0x1008: slli   t0, t0, 0x1f */
	reset_vec[3] = 0x00028067; /* 0x100c: jr     t0 */
	reset_vec[0] = reset_vec[4] = 0;

	/* copy in the reset vector in little_endian byte order */
	for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
		reset_vec[i] = cpu_to_le32(reset_vec[i]);
	}
	rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec), memmap[G233_DEV_MROM].base,
			      &address_space_memory);

	riscv_boot_info_init(&boot_info, &s->soc.cpus);
	if (machine->kernel_filename) {
		riscv_load_kernel(machine, &boot_info, memmap[G233_DEV_DRAM].base, false, NULL);
	}
	flash_dev = qdev_new("w25x16");
	/* Get block driver from command line */
	BlockDriverState *bs = bdrv_lookup_bs(NULL, "flash0", &error_fatal);
	if (bs) {
		blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
		blk_insert_bs(blk, bs, &error_fatal);
	}
	qdev_prop_set_drive_err(flash_dev, "driver", blk, &error_fatal);
	qdev_prop_set_uint32(flash_dev, "cs", 0);
	// qdev_realize_and_unref(flash_dev, BUS(&soc.spi0.ssi), &error_fatal);
	//cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
	//sysbus_connect_irq(SYS_BUS_DEVICE(&soc.spi0), 0, cs_line);
}

static void g233_machine_instance_init(Object *obj)
{
	// G233MachineState *s = RISCV_G233_MACHINE(obj);
}

static void g233_machine_class_init(ObjectClass *oc, const void *data)
{
	MachineClass *mc = MACHINE_CLASS(oc);

	mc->desc = "QEMU RISC-V G233 Board with Learning QEMU 2025";
	mc->init = g233_machine_init;
	mc->max_cpus = 1;
	mc->default_cpu_type = TYPE_RISCV_CPU_GEVICO_G233;
	mc->default_ram_id = "riscv.g233.ram"; /* DDR */
	mc->default_ram_size = g233_memmap[G233_DEV_DRAM].size;
}

static const TypeInfo g233_types[] = { {
					       .name = TYPE_RISCV_G233_SOC,
					       .parent = TYPE_DEVICE,
					       .class_init = g233_soc_class_init,
					       .instance_init = g233_soc_instance_init,
					       .instance_size = sizeof(G233SoCState),

				       },
				       {
					       .name = TYPE_RISCV_G233_MACHINE,
					       .parent = TYPE_MACHINE,
					       .class_init = g233_machine_class_init,
					       .instance_init = g233_machine_instance_init,
					       .instance_size = sizeof(G233MachineState),
				       } };

DEFINE_TYPES(g233_types);
