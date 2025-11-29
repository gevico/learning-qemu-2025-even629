#ifndef HW_G233_SPI_H
#define HW_G233_SPI_H

#include "hw/sysbus.h"
#include "qemu/fifo8.h"

// 0x00	SPI_CR1	    R/W	  0x00000000	控制寄存器 1
#define G233_SPI_CR1 0x0
// 0x04	SPI_CR2	    R/W	  0x00000000	控制寄存器 2
#define G233_SPI_CR2 0x4
// 0x08	SPI_SR	    R/W	  0x00000002	状态寄存器
#define G233_SPI_SR 0x8
// 0x0C	SPI_DR	    R/W	  0x0000000C	数据寄存器
#define G233_SPI_DR 0xc
// 0x10	SPI_CSCTRL  R/W	  0x00000000	CS 控制寄存器
#define G233_SPI_CSCTRL 0x10

enum {
	G233_SPI_CR1_IDX = 0,
	G233_SPI_CR2_IDX,
	G233_SPI_SR_IDX,
	G233_SPI_DR_IDX,
	G233_SPI_CSCTRL_IDX,
	G233_SPI_REG_NUM
};

/* CR1  */
// SPI 使能位 0: SPI 禁用, 1: SPI 使能
#define G233_SPI_CR1_SPE_MASK BIT(6)
// 主模式选择位 0: 从模式 1: 主模式
#define G233_SPI_CR1_MSTR_MASK BIT(2)

#define G233_SPI_CR1_MASK (G233_SPI_CR1_SPE_MASK | G233_SPI_CR1_MSTR_MASK)

/* CR2 interrupt bits */
// TXE 中断使能位 0: TXE 中断禁用 1: TXE 中断使能
#define G233_SPI_CR2_TXEIE_MASK BIT(7)
// RXNE 中断使能位 0: RXNE 中断禁用 1: RXNE 中断使能
#define G233_SPI_CR2_RXNEIE_MASK BIT(6)
// 错误中断使能位 0: 错误中断禁用 1: 错误中断使能
#define G233_SPI_CR2_ERRIE_MASK BIT(5)
// 软件从设备选择输出使能 0: SS 输出禁用 1: SS 输出使能
#define G233_SPI_CR2_SSOE_MASK BIT(4)

#define G233_SPI_CR2_MASK \
	(G233_SPI_CR2_TXEIE_MASK | G233_SPI_CR2_RXNEIE_MASK | G233_SPI_CR2_ERRIE_MASK | G233_SPI_CR2_SSOE_MASK)
/* SR bits */
// 忙标志位 0: SPI 空闲 1: SPI 忙
#define G233_SPI_SR_BSY_MASK BIT(7)
// 溢出错误标志 0: 无溢出 1: 发生溢出(写 1 清除)
#define G233_SPI_SR_OVERRUN_MASK BIT(3)
// 下溢错误标志 0: 无下溢 1: 发生下溢（写 1 清除）
#define G233_SPI_SR_UNDERRUN_MASK BIT(2)
// 发送缓冲区空标志 0: 发送缓冲区满 1: 发送缓冲区空
#define G233_SPI_SR_TXE_MASK BIT(1)
// 接收缓冲区非空标志 0: 接收缓冲区为空 1: 接收缓冲区非空，数据可读
#define G233_SPI_SR_RXNE_MASK BIT(0)

#define G233_SPI_SR_MASK                                                                                      \
	(G233_SPI_SR_BSY_MASK | G233_SPI_SR_OVERRUN_MASK | G233_SPI_SR_UNDERRUN_MASK | G233_SPI_SR_TXE_MASK | \
	 G233_SPI_SR_RXNE_MASK)
/* DR */
// 数据位 写入：发送数据 读取：接收数据
#define G233_SPI_DR_DATA_MASK (0xff)
#define G233_SPI_DR_MASK G233_SPI_DR_DATA_MASK
/* CSCTRL */
//CS3 激活状态 0: 非激活 1: 激活
#define G233_SPI_CSCTRL_CS3_ACT_MASK BIT(7)
//CS2 激活状态 0: 非激活 1: 激活
#define G233_SPI_CSCTRL_CS2_ACT_MASK BIT(6)
//CS1 激活状态 0: 非激活 1: 激活
#define G233_SPI_CSCTRL_CS1_ACT_MASK BIT(5)
//CS0 激活状态 0: 非激活 1: 激活
#define G233_SPI_CSCTRL_CS0_ACT_MASK BIT(4)

//CS3 使能位 0: 禁用 1: 使能
#define G233_SPI_CSCTRL_CS3_EN_MASK BIT(3)
//CS2 使能位 0: 禁用 1: 使能
#define G233_SPI_CSCTRL_CS2_EN_MASK BIT(2)
//CS1 使能位 0: 禁用 1: 使能
#define G233_SPI_CSCTRL_CS1_EN_MASK BIT(1)
//CS0 使能位 0: 禁用 1: 使能
#define G233_SPI_CSCTRL_CS0_EN_MASK BIT(0)

#define G233_SPI_CSCTRL_MASK 0xff

#define FIFO_CAPACITY 1

#define TYPE_G233_SPI "g233.spi"
#define G233_SPI(obj) OBJECT_CHECK(G233SPIState, (obj), TYPE_G233_SPI)

typedef struct G233SPIState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;
	qemu_irq irq;

	uint32_t num_cs;
	qemu_irq *cs_lines;

	SSIBus *ssi;

	Fifo8 tx_fifo;
	Fifo8 rx_fifo;

	uint32_t regs[G233_SPI_REG_NUM];
} G233SPIState;

#endif
