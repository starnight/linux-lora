/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Semtech SX1301 LoRa concentrator
 *
 * Copyright (c) 2018 Ben Whitten
 * Copyright (c) 2018 Andreas Färber
 */

#ifndef _SX130X_
#define _SX130X_

#include <linux/regmap.h>

#define SX1301_CHIP_VERSION 103

#define SX1301_MCU_FW_BYTE 8192
#define SX1301_MCU_ARB_FW_VERSION 1
#define SX1301_MCU_AGC_FW_VERSION 4
#define SX1301_MCU_AGC_CAL_FW_VERSION 2

/* Page independent */
#define SX1301_PAGE     0x00
#define SX1301_VER      0x01
#define SX1301_MPA      0x09
#define SX1301_MPD      0x0A
#define SX1301_GEN      0x10
#define SX1301_CKEN     0x11
#define SX1301_GPSO     0x1C
#define SX1301_GPMODE   0x1D
#define SX1301_AGCSTS   0x20

#define SX1301_VIRT_BASE    0x100
#define SX1301_PAGE_LEN     0x80
#define SX1301_PAGE_BASE(n) (SX1301_VIRT_BASE + (SX1301_PAGE_LEN * n))

/* Page 0 */
#define SX1301_CHRS         (SX1301_PAGE_BASE(0) + 0x23)
#define SX1301_FORCE_CTRL   (SX1301_PAGE_BASE(0) + 0x69)
#define SX1301_MCU_CTRL     (SX1301_PAGE_BASE(0) + 0x6A)

/* Page 2 */
#define SX1301_RADIO_A_SPI_DATA     (SX1301_PAGE_BASE(2) + 0x21)
#define SX1301_RADIO_A_SPI_DATA_RB  (SX1301_PAGE_BASE(2) + 0x22)
#define SX1301_RADIO_A_SPI_ADDR     (SX1301_PAGE_BASE(2) + 0x23)
#define SX1301_RADIO_A_SPI_CS       (SX1301_PAGE_BASE(2) + 0x25)
#define SX1301_RADIO_B_SPI_DATA     (SX1301_PAGE_BASE(2) + 0x26)
#define SX1301_RADIO_B_SPI_DATA_RB  (SX1301_PAGE_BASE(2) + 0x27)
#define SX1301_RADIO_B_SPI_ADDR     (SX1301_PAGE_BASE(2) + 0x28)
#define SX1301_RADIO_B_SPI_CS       (SX1301_PAGE_BASE(2) + 0x2A)
#define SX1301_RADIO_CFG            (SX1301_PAGE_BASE(2) + 0x2B)
#define SX1301_DBG_ARB_MCU_RAM_DATA (SX1301_PAGE_BASE(2) + 0x40)
#define SX1301_DBG_AGC_MCU_RAM_DATA (SX1301_PAGE_BASE(2) + 0x41)
#define SX1301_DBG_ARB_MCU_RAM_ADDR (SX1301_PAGE_BASE(2) + 0x50)
#define SX1301_DBG_AGC_MCU_RAM_ADDR (SX1301_PAGE_BASE(2) + 0x51)

/* Page 3 */
#define SX1301_EMERGENCY_FORCE_HOST_CTRL (SX1301_PAGE_BASE(3) + 0x7F)

#define SX1301_MAX_REGISTER         (SX1301_PAGE_BASE(3) + 0x7F)

enum sx130x_fields {
	F_SOFT_RESET,
	F_GLOBAL_EN,
	F_CLK32M_EN,
	F_RADIO_A_EN,
	F_RADIO_B_EN,
	F_RADIO_RST,

	F_MCU_RST_0,
	F_MCU_RST_1,
	F_MCU_SELECT_MUX_0,
	F_MCU_SELECT_MUX_1,

	F_FORCE_HOST_RADIO_CTRL,
	F_FORCE_HOST_FE_CTRL,
	F_FORCE_DEC_FILTER_GAIN,

	F_EMERGENCY_FORCE_HOST_CTRL,
};

struct regmap *sx130x_get_regmap(struct device *dev);
void sx130x_io_lock(struct device *dev);
void sx130x_io_unlock(struct device *dev);

int __init sx130x_radio_init(void);
void __exit sx130x_radio_exit(void);
int sx130x_register_radio_devices(struct device *dev);
int devm_sx130x_register_radio_devices(struct device *dev);
void sx130x_unregister_radio_devices(struct device *dev);
bool sx130x_radio_devices_okay(struct device *dev);

#endif
