#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/typedefs.h"
#include <stdint.h>
#include "hw/ssi/g233_spi.h"

static void g233_spi_update_irq(G233SPIState *s)
{
	int level = 0;
	if (fifo8_is_empty(&s->tx_fifo)) {
		s->regs[G233_SPI_SR_IDX] |= G233_SPI_SR_TXE_MASK;
		if (s->regs[G233_SPI_CR2_IDX] & G233_SPI_CR2_TXEIE_MASK) {
			level = 1;
		}
	} else { //FIFO size: 1
		s->regs[G233_SPI_SR_IDX] &= ~G233_SPI_SR_TXE_MASK;
	}

	if (!fifo8_is_empty(&s->rx_fifo)) {
		s->regs[G233_SPI_SR_IDX] |= G233_SPI_SR_RXNE_MASK;
		if (s->regs[G233_SPI_CR2_IDX] & G233_SPI_CR2_RXNEIE_MASK) {
			level = 1;
		}
	} else {
		s->regs[G233_SPI_SR_IDX] &= ~G233_SPI_SR_RXNE_MASK;
	}

	// 使能了错误中断情况下发生了溢出错误或者下溢错误
	if ((s->regs[G233_SPI_CR2_IDX] & G233_SPI_CR2_ERRIE_MASK) &&
	    (s->regs[G233_SPI_SR_IDX] & (G233_SPI_SR_OVERRUN_MASK | G233_SPI_SR_UNDERRUN_MASK))) {
		level = 1;
	}
	qemu_set_irq(s->irq, level);
}

static void g233_spi_update_cs(G233SPIState *s)
{
	int i;
	bool enable, active;
	for (i = 0; i < s->num_cs; i++) {
		enable = s->regs[G233_SPI_CSCTRL_IDX] & (1 << i);
		active = s->regs[G233_SPI_CSCTRL_IDX] & (1 << (4 + i));
		if (enable) {
			qemu_set_irq(s->cs_lines[i], !active);
		} else {
			qemu_irq_raise(s->cs_lines[i]);
		}
	}
}

static void g233_spi_flush_tx(G233SPIState *s)
{
	uint8_t tx_data, rx_data;
	s->regs[G233_SPI_SR_IDX] |= G233_SPI_SR_BSY_MASK;

	while (!fifo8_is_empty(&s->tx_fifo)) {
		tx_data = fifo8_pop(&s->tx_fifo);

		/* 通过 SSIBus 发送给挂载的设备 */
		rx_data = ssi_transfer(s->ssi, tx_data);

		/* 写入 RX FIFO */
		if (!fifo8_is_full(&s->rx_fifo)) {
			fifo8_push(&s->rx_fifo, rx_data);
		} else {
			s->regs[G233_SPI_SR_IDX] |= G233_SPI_SR_OVERRUN_MASK;
		}
	}

	s->regs[G233_SPI_SR_IDX] &= ~G233_SPI_SR_BSY_MASK;
}

/* ------------ Register Read ---------------- */
static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
	G233SPIState *s = opaque;
	uint32_t r;

	switch (addr) {
	case G233_SPI_DR:
		if (fifo8_is_empty(&s->rx_fifo)) { //rx为空
			s->regs[G233_SPI_SR_IDX] |= G233_SPI_SR_UNDERRUN_MASK;
			r = 0;
		} else {
			r = fifo8_pop(&s->rx_fifo);
		}
		break;
	case G233_SPI_CR1:
	case G233_SPI_CR2:
	case G233_SPI_SR:
	case G233_SPI_CSCTRL:
		r = s->regs[addr >> 2];
		break;
	default:
		r = 0;
		qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at address 0x%" HWADDR_PRIx "\n", __func__,
			      (unsigned long)addr);
		break;
	}
	g233_spi_update_irq(s);
	return r;
}

/* ------------ Register Write ---------------- */
static void g233_spi_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
	G233SPIState *s = opaque;
	// TODO 保留位必须为0
	switch (addr) {
	case G233_SPI_CR1:
		s->regs[addr >> 2] = value & G233_SPI_CR1_MASK;
		break;
	case G233_SPI_CR2:
		s->regs[addr >> 2] = value & G233_SPI_CR2_MASK;
		break;
	case G233_SPI_SR: //清除error标志位
		s->regs[addr >> 2] &= ~(value & (G233_SPI_SR_UNDERRUN_MASK | G233_SPI_SR_OVERRUN_MASK));
		break;
	case G233_SPI_CSCTRL:
		s->regs[addr >> 2] = value & G233_SPI_CSCTRL_MASK;
		g233_spi_update_cs(s);
		break;
	case G233_SPI_DR:
		if (!fifo8_is_full(&s->tx_fifo)) {
			fifo8_push(&s->tx_fifo, (uint8_t)value);
			g233_spi_flush_tx(s);
		} else {
			s->regs[G233_SPI_SR_IDX] |= G233_SPI_SR_OVERRUN_MASK;
		}
		break;
	default:
		qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at address 0x%" HWADDR_PRIx "\n", __func__,
			      (unsigned long)addr);
		break;
	}
	g233_spi_update_irq(s);
}

static const MemoryRegionOps g233_spi_ops = {
	.read = g233_spi_read,
	.write = g233_spi_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
	SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
	G233SPIState *s = G233_SPI(dev);
	int i;

	s->ssi = ssi_create_bus(dev, "spi");
	sysbus_init_irq(sbd, &s->irq);

	s->cs_lines = g_new0(qemu_irq, s->num_cs);
	for (i = 0; i < s->num_cs; i++) {
		sysbus_init_irq(sbd, &s->cs_lines[i]);
	}

	memory_region_init_io(&s->mmio, OBJECT(s), &g233_spi_ops, s, TYPE_G233_SPI,
			      sizeof(uint32_t) * G233_SPI_REG_NUM);
	sysbus_init_mmio(sbd, &s->mmio);
}

static const Property g233_spi_properties[] = {
	DEFINE_PROP_UINT32("num-cs", G233SPIState, num_cs, 4),
};

static void g233_spi_reset(DeviceState *d)
{
	G233SPIState *s = G233_SPI(d);

	memset(s->regs, 0, sizeof(s->regs));

	s->regs[G233_SPI_CR1_IDX] = 0x0;
	s->regs[G233_SPI_CR2_IDX] = 0x0;
	s->regs[G233_SPI_SR_IDX] = 0x2;
	s->regs[G233_SPI_DR_IDX] = 0x0c;
	s->regs[G233_SPI_CSCTRL_IDX] = 0x0;

	fifo8_reset(&s->tx_fifo);
	fifo8_reset(&s->rx_fifo);

	g233_spi_update_cs(s);
	g233_spi_update_irq(s);
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, g233_spi_properties);
	device_class_set_legacy_reset(dc, g233_spi_reset);
	dc->realize = g233_spi_realize;
}

static void g233_spi_instance_init(Object *obj)
{
	G233SPIState *s = G233_SPI(obj);
	fifo8_create(&s->tx_fifo, FIFO_CAPACITY);
	fifo8_create(&s->rx_fifo, FIFO_CAPACITY);
}

static const TypeInfo g233_spi_register_types[] = { {
	.name = TYPE_G233_SPI,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(G233SPIState),
	.instance_init = g233_spi_instance_init,
	.class_init = g233_spi_class_init,
} };

DEFINE_TYPES(g233_spi_register_types);
