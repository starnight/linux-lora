// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Semtech SX1301 LoRa concentrator
 *
 * Copyright (c) 2018 Andreas Färber
 * Copyright (c) 2018 Ben Whitten
 *
 * Based on SX1301 HAL code:
 * Copyright (c) 2013 Semtech-Cycleo
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/lora.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/lora/dev.h>
#include <linux/spi/spi.h>

#include "sx130x.h"

static const struct reg_field sx130x_regmap_fields[] = {
	/* PAGE */
	[F_SOFT_RESET]          = REG_FIELD(SX1301_PAGE, 7, 7),
	/* GEN */
	[F_GLOBAL_EN]           = REG_FIELD(SX1301_GEN,  3, 3),
	/* CKEN */
	[F_CLK32M_EN]           = REG_FIELD(SX1301_CKEN, 0, 0),
	/* RADIO_CFG */
	[F_RADIO_A_EN]          = REG_FIELD(SX1301_RADIO_CFG, 0, 0),
	[F_RADIO_B_EN]          = REG_FIELD(SX1301_RADIO_CFG, 1, 1),
	[F_RADIO_RST]           = REG_FIELD(SX1301_RADIO_CFG, 2, 2),
	/* MCU_CTRL */
	[F_MCU_RST_0]           = REG_FIELD(SX1301_MCU_CTRL, 0, 0),
	[F_MCU_RST_1]           = REG_FIELD(SX1301_MCU_CTRL, 1, 1),
	[F_MCU_SELECT_MUX_0]    = REG_FIELD(SX1301_MCU_CTRL, 2, 2),
	[F_MCU_SELECT_MUX_1]    = REG_FIELD(SX1301_MCU_CTRL, 3, 3),
	/* FORCE_CTRL */
	[F_FORCE_HOST_RADIO_CTRL] = REG_FIELD(SX1301_FORCE_CTRL, 1, 1),
	[F_FORCE_HOST_FE_CTRL]    = REG_FIELD(SX1301_FORCE_CTRL, 2, 2),
	[F_FORCE_DEC_FILTER_GAIN] = REG_FIELD(SX1301_FORCE_CTRL, 3, 3),
	/* EMERGENCY_FORCE_HOST_CTRL */
	[F_EMERGENCY_FORCE_HOST_CTRL] =
		REG_FIELD(SX1301_EMERGENCY_FORCE_HOST_CTRL, 0, 0),
};

struct sx130x_priv {
	struct lora_dev_priv	lora;
	struct device		*dev;
	struct clk		*clk32m;
	struct gpio_desc	*rst_gpio;
	struct regmap		*regmap;
	struct regmap_field	*regmap_fields[ARRAY_SIZE(sx130x_regmap_fields)];
	struct mutex		io_lock;
	void			*drvdata;
};

struct regmap *sx130x_get_regmap(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct sx130x_priv *priv = netdev_priv(netdev);

	return priv->regmap;
}

void sx130x_set_drvdata(struct device *dev, void *drvdata)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct sx130x_priv *priv = netdev_priv(netdev);

	priv->drvdata = drvdata;
}
EXPORT_SYMBOL_GPL(sx130x_set_drvdata);

void *sx130x_get_drvdata(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct sx130x_priv *priv = netdev_priv(netdev);

	return priv->drvdata;
}
EXPORT_SYMBOL_GPL(sx130x_get_drvdata);

void sx130x_io_lock(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct sx130x_priv *priv = netdev_priv(netdev);

	mutex_lock(&priv->io_lock);
}

void sx130x_io_unlock(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct sx130x_priv *priv = netdev_priv(netdev);

	mutex_unlock(&priv->io_lock);
}

static const struct regmap_range_cfg sx130x_regmap_ranges[] = {
	{
		.name = "Pages",

		.range_min = SX1301_VIRT_BASE,
		.range_max = SX1301_MAX_REGISTER,

		.selector_reg = SX1301_PAGE,
		.selector_mask = 0x3,

		.window_start = 0,
		.window_len = SX1301_PAGE_LEN,
	},
};

static bool sx130x_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SX1301_DBG_AGC_MCU_RAM_DATA:
	case SX1301_DBG_ARB_MCU_RAM_DATA:
	case SX1301_MPD:
	case SX1301_RADIO_A_SPI_DATA_RB:
	case SX1301_RADIO_B_SPI_DATA_RB:
		return true;
	default:
		return false;
	}
}

static bool sx130x_writeable_noinc_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SX1301_MPD:
		return true;
	default:
		return false;
	}
}

static bool sx130x_readable_noinc_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SX1301_MPD:
		return true;
	default:
		return false;
	}
}

const struct regmap_config sx130x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_NONE,
	.disable_locking = true,

	.read_flag_mask = 0,
	.write_flag_mask = BIT(7),

	.volatile_reg = sx130x_volatile_reg,
	.writeable_noinc_reg = sx130x_writeable_noinc_reg,
	.readable_noinc_reg = sx130x_readable_noinc_reg,

	.ranges = sx130x_regmap_ranges,
	.num_ranges = ARRAY_SIZE(sx130x_regmap_ranges),
	.max_register = SX1301_MAX_REGISTER,
};
EXPORT_SYMBOL_GPL(sx130x_regmap_config);

static inline int sx130x_field_write(struct sx130x_priv *priv,
		enum sx130x_fields field_id, u8 val)
{
	return regmap_field_write(priv->regmap_fields[field_id], val);
}

static inline int sx130x_field_force_write(struct sx130x_priv *priv,
		enum sx130x_fields field_id, u8 val)
{
	return regmap_field_force_write(priv->regmap_fields[field_id], val);
}

static int sx130x_soft_reset(struct sx130x_priv *priv)
{
	int ret;

	regcache_cache_bypass(priv->regmap, true);
	ret = sx130x_field_write(priv, F_SOFT_RESET, 1);
	regcache_cache_bypass(priv->regmap, false);
	if (ret)
		return ret;

	regcache_mark_dirty(priv->regmap);
	if (sx130x_regmap_config.cache_type != REGCACHE_NONE)
		return regcache_drop_region(priv->regmap,
			0, sx130x_regmap_config.max_register);
	return 0;
}

static int sx130x_agc_ram_read(struct sx130x_priv *priv, u8 addr, unsigned int *val)
{
	int ret;

	ret = regmap_write(priv->regmap, SX1301_DBG_AGC_MCU_RAM_ADDR, addr);
	if (ret) {
		dev_err(priv->dev, "AGC RAM addr write failed\n");
		return ret;
	}

	ret = regmap_read(priv->regmap, SX1301_DBG_AGC_MCU_RAM_DATA, val);
	if (ret) {
		dev_err(priv->dev, "AGC RAM data read failed\n");
		return ret;
	}

	return 0;
}

static int sx130x_arb_ram_read(struct sx130x_priv *priv, u8 addr, unsigned int *val)
{
	int ret;

	ret = regmap_write(priv->regmap, SX1301_DBG_ARB_MCU_RAM_ADDR, addr);
	if (ret) {
		dev_err(priv->dev, "ARB RAM addr write failed\n");
		return ret;
	}

	ret = regmap_read(priv->regmap, SX1301_DBG_ARB_MCU_RAM_DATA, val);
	if (ret) {
		dev_err(priv->dev, "ARB RAM data read failed\n");
		return ret;
	}

	return 0;
}

static int sx130x_load_firmware(struct sx130x_priv *priv, int mcu, const struct firmware *fw)
{
	u8 *buf;
	enum sx130x_fields rst, select_mux;
	unsigned int val;
	int ret;

	if (fw->size != SX1301_MCU_FW_BYTE) {
		dev_err(priv->dev, "Unexpected firmware size\n");
		return -EINVAL;
	}

	switch (mcu) {
	case 0:
		rst = F_MCU_RST_0;
		select_mux = F_MCU_SELECT_MUX_0;
		break;
	case 1:
		rst = F_MCU_RST_1;
		select_mux = F_MCU_SELECT_MUX_1;
		break;
	default:
		return -EINVAL;
	}

	ret = sx130x_field_write(priv, rst, 1);
	if (ret) {
		dev_err(priv->dev, "MCU reset failed\n");
		return ret;
	}

	ret = sx130x_field_write(priv, select_mux, 0);
	if (ret) {
		dev_err(priv->dev, "MCU RAM select mux failed\n");
		return ret;
	}

	ret = regmap_write(priv->regmap, SX1301_MPA, 0);
	if (ret) {
		dev_err(priv->dev, "MCU prom addr write failed\n");
		return ret;
	}

	ret = regmap_noinc_write(priv->regmap, SX1301_MPD, fw->data, fw->size);
	if (ret) {
		dev_err(priv->dev, "MCU prom data write failed\n");
		return ret;
	}

	ret = regmap_read(priv->regmap, SX1301_MPD, &val);
	if (ret) {
		dev_err(priv->dev, "MCU prom data dummy read failed\n");
		return ret;
	}

	buf = kzalloc(fw->size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = regmap_noinc_read(priv->regmap, SX1301_MPD, buf, fw->size);
	if (ret) {
		dev_err(priv->dev, "MCU prom data read failed\n");
		kfree(buf);
		return ret;
	}

	if (memcmp(fw->data, buf, fw->size)) {
		dev_err(priv->dev, "MCU prom data read does not match data written\n");
		kfree(buf);
		return -ENXIO;
	}

	kfree(buf);

	ret = sx130x_field_force_write(priv, select_mux, 1);
	if (ret) {
		dev_err(priv->dev, "MCU RAM release mux failed\n");
		return ret;
	}

	return 0;
}

static int sx130x_agc_calibrate(struct sx130x_priv *priv)
{
	const struct firmware *fw;
	unsigned int val;
	int ret;

	ret = request_firmware(&fw, "sx1301_agc_calibration.bin", priv->dev);
	if (ret) {
		dev_err(priv->dev, "agc cal firmware file load failed\n");
		return ret;
	}

	ret = sx130x_load_firmware(priv, 1, fw);
	release_firmware(fw);
	if (ret) {
		dev_err(priv->dev, "agc cal firmware load failed\n");
		return ret;
	}

	ret = sx130x_field_write(priv, F_FORCE_HOST_RADIO_CTRL, 0);
	if (ret) {
		dev_err(priv->dev, "force host control failed\n");
		return ret;
	}

	val = BIT(4); /* with DAC gain=3 */
	if (false)
		val |= BIT(5); /* SX1255 */

	ret = regmap_write(priv->regmap, SX1301_CHRS, val);
	if (ret) {
		dev_err(priv->dev, "radio select write failed\n");
		return ret;
	}

	ret = sx130x_field_force_write(priv, F_MCU_RST_1, 0);
	if (ret) {
		dev_err(priv->dev, "MCU 1 reset failed\n");
		return ret;
	}

	ret = sx130x_agc_ram_read(priv, 0x20, &val);
	if (ret) {
		dev_err(priv->dev, "AGC RAM data read failed\n");
		return ret;
	}

	dev_info(priv->dev, "AGC calibration firmware version %u\n", (unsigned)val);

	if (val != SX1301_MCU_AGC_CAL_FW_VERSION) {
		dev_err(priv->dev, "unexpected firmware version, expecting %u\n",
				SX1301_MCU_AGC_CAL_FW_VERSION);
		return -EIO;
	}

	ret = sx130x_field_force_write(priv, F_EMERGENCY_FORCE_HOST_CTRL, 0);
	if (ret) {
		dev_err(priv->dev, "emergency force failed\n");
		return ret;
	}

	dev_err(priv->dev, "starting calibration...\n");
	msleep(2300);

	ret = sx130x_field_write(priv, F_EMERGENCY_FORCE_HOST_CTRL, 1);
	if (ret) {
		dev_err(priv->dev, "emergency force release failed\n");
		return ret;
	}

	ret = regmap_read(priv->regmap, SX1301_AGCSTS, &val);
	if (ret) {
		dev_err(priv->dev, "AGC status read failed\n");
		return ret;
	}

	dev_info(priv->dev, "AGC status: %02x\n", (unsigned)val);
	if ((val & (BIT(7) | BIT(0))) != (BIT(7) | BIT(0))) {
		dev_err(priv->dev, "AGC calibration failed\n");
		return -EIO;
	}

	return 0;
}

static int sx130x_load_all_firmware(struct sx130x_priv *priv)
{
	const struct firmware *fw;
	unsigned int val;
	int ret;

	ret = request_firmware(&fw, "sx1301_arb.bin", priv->dev);
	if (ret) {
		dev_err(priv->dev, "arb firmware file load failed\n");
		return ret;
	}

	ret = sx130x_load_firmware(priv, 0, fw);
	release_firmware(fw);
	if (ret)
		return ret;

	ret = request_firmware(&fw, "sx1301_agc.bin", priv->dev);
	if (ret) {
		dev_err(priv->dev, "agc firmware file load failed\n");
		return ret;
	}

	ret = sx130x_load_firmware(priv, 1, fw);
	release_firmware(fw);
	if (ret)
		return ret;

	ret = sx130x_field_write(priv, F_FORCE_HOST_RADIO_CTRL, 0);
	if (ret)
		return ret;
	ret = sx130x_field_write(priv, F_FORCE_HOST_FE_CTRL, 0);
	if (ret)
		return ret;
	ret = sx130x_field_write(priv, F_FORCE_DEC_FILTER_GAIN, 0);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, SX1301_CHRS, 0);
	if (ret) {
		dev_err(priv->dev, "radio select write failed\n");
		return ret;
	}

	ret = sx130x_field_write(priv, F_MCU_RST_0, 0);
	if (ret) {
		dev_err(priv->dev, "MCU 0 release failed\n");
		return ret;
	}

	ret = sx130x_field_force_write(priv, F_MCU_RST_1, 0);
	if (ret) {
		dev_err(priv->dev, "MCU 1 release failed\n");
		return ret;
	}

	ret = sx130x_agc_ram_read(priv, 0x20, &val);
	if (ret) {
		dev_err(priv->dev, "AGC RAM data read failed\n");
		return ret;
	}

	dev_info(priv->dev, "AGC firmware version %u\n", (unsigned)val);

	if (val != SX1301_MCU_AGC_FW_VERSION) {
		dev_err(priv->dev, "unexpected firmware version, expecting %u\n",
				SX1301_MCU_AGC_FW_VERSION);
		return -EIO;
	}

	ret = sx130x_arb_ram_read(priv, 0x20, &val);
	if (ret) {
		dev_err(priv->dev, "ARB RAM data read failed\n");
		return ret;
	}

	dev_info(priv->dev, "ARB firmware version %u\n", (unsigned)val);

	if (val != SX1301_MCU_ARB_FW_VERSION) {
		dev_err(priv->dev, "unexpected firmware version, expecting %u\n",
				SX1301_MCU_ARB_FW_VERSION);
		return -EIO;
	}

	return 0;
}

static netdev_tx_t sx130x_loradev_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	if (skb->protocol != htons(ETH_P_LORA)) {
		kfree_skb(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	netif_stop_queue(netdev);

	/* TODO */
	return NETDEV_TX_OK;
}

static int sx130x_loradev_open(struct net_device *netdev)
{
	struct sx130x_priv *priv = netdev_priv(netdev);
	int ret;

	netdev_dbg(netdev, "%s", __func__);

	if (!sx130x_radio_devices_okay(priv->dev)) {
		netdev_err(netdev, "radio devices not yet bound to a driver\n");
		return -ENXIO;
	}

	priv->clk32m = clk_get(priv->dev, "clk32m");
	if (IS_ERR(priv->clk32m)) {
		dev_err(priv->dev, "failed to get clk32m (%ld)\n", PTR_ERR(priv->clk32m));
		return PTR_ERR(priv->clk32m);
	}

	ret = clk_prepare_enable(priv->clk32m);
	if (ret) {
		dev_err(priv->dev, "failed to enable clk32m (%d)\n", ret);
		goto err_clk_enable;
	}

	mutex_lock(&priv->io_lock);

	ret = sx130x_field_write(priv, F_GLOBAL_EN, 1);
	if (ret) {
		dev_err(priv->dev, "enable global clocks failed (%d)\n", ret);
		goto err_reg;
	}

	ret = sx130x_field_force_write(priv, F_CLK32M_EN, 1);
	if (ret) {
		dev_err(priv->dev, "enable 32M clock failed (%d)\n", ret);
		goto err_reg;
	}

	/* calibration */

	ret = sx130x_agc_calibrate(priv);
	if (ret)
		goto err_calibrate;

	/* TODO */

	ret = sx130x_load_all_firmware(priv);
	if (ret)
		goto err_firmware;

	ret = open_loradev(netdev);
	if (ret)
		goto err_open;

	mutex_unlock(&priv->io_lock);

	netif_start_queue(netdev);

	return 0;

err_open:
err_firmware:
err_calibrate:
err_reg:
	mutex_unlock(&priv->io_lock);
	clk_disable_unprepare(priv->clk32m);
err_clk_enable:
	clk_put(priv->clk32m);
	return ret;
}

static int sx130x_loradev_stop(struct net_device *netdev)
{
	struct sx130x_priv *priv = netdev_priv(netdev);

	netdev_dbg(netdev, "%s", __func__);

	netif_stop_queue(netdev);
	close_loradev(netdev);

	clk_disable_unprepare(priv->clk32m);
	clk_put(priv->clk32m);
	priv->clk32m = NULL;

	return 0;
}

static const struct net_device_ops sx130x_net_device_ops = {
	.ndo_open = sx130x_loradev_open,
	.ndo_stop = sx130x_loradev_stop,
	.ndo_start_xmit = sx130x_loradev_start_xmit,
};

int sx130x_early_probe(struct regmap *regmap, struct gpio_desc *rst)
{
	struct device *dev = regmap_get_device(regmap);
	struct net_device *netdev;
	struct sx130x_priv *priv;
	int ret;
	int i;

	netdev = devm_alloc_loradev(dev, sizeof(*priv));
	if (!netdev)
		return -ENOMEM;

	netdev->netdev_ops = &sx130x_net_device_ops;
	SET_NETDEV_DEV(netdev, dev);

	priv = netdev_priv(netdev);
	priv->regmap = regmap;
	priv->rst_gpio = rst;

	mutex_init(&priv->io_lock);

	dev_set_drvdata(dev, netdev);
	priv->dev = dev;

	for (i = 0; i < ARRAY_SIZE(sx130x_regmap_fields); i++) {
		const struct reg_field *reg_fields = sx130x_regmap_fields;

		priv->regmap_fields[i] = devm_regmap_field_alloc(dev,
				priv->regmap,
				reg_fields[i]);
		if (IS_ERR(priv->regmap_fields[i])) {
			ret = PTR_ERR(priv->regmap_fields[i]);
			dev_err(dev, "Cannot allocate regmap field (%d)\n", ret);
			return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sx130x_early_probe);

int sx130x_probe(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);
	struct sx130x_priv *priv = netdev_priv(netdev);
	unsigned int ver;
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, SX1301_VER, &ver);
	if (ret) {
		dev_err(dev, "version read failed (%d)\n", ret);
		return ret;
	}

	if (ver != SX1301_CHIP_VERSION) {
		dev_err(dev, "unexpected version: %u\n", ver);
		return -EIO;
	}

	ret = regmap_write(priv->regmap, SX1301_PAGE, 0);
	if (ret) {
		dev_err(dev, "page/reset write failed (%d)\n", ret);
		return ret;
	}

	ret = sx130x_soft_reset(priv);
	if (ret) {
		dev_err(dev, "soft reset failed (%d)\n", ret);
		return ret;
	}

	ret = sx130x_field_write(priv, F_GLOBAL_EN, 0);
	if (ret) {
		dev_err(dev, "gate global clocks failed (%d)\n", ret);
		return ret;
	}

	ret = sx130x_field_write(priv, F_CLK32M_EN, 0);
	if (ret) {
		dev_err(dev, "gate 32M clock failed (%d)\n", ret);
		return ret;
	}

	ret = sx130x_field_write(priv, F_RADIO_A_EN, 1);
	if (ret) {
		dev_err(dev, "radio A enable failed (%d)\n", ret);
		return ret;
	}

	ret = sx130x_field_force_write(priv, F_RADIO_B_EN, 1);
	if (ret) {
		dev_err(dev, "radio B enable failed (%d)\n", ret);
		return ret;
	}

	msleep(500);

	ret = sx130x_field_force_write(priv, F_RADIO_RST, 1);
	if (ret) {
		dev_err(dev, "radio assert reset failed (%d)\n", ret);
		return ret;
	}

	msleep(5);

	ret = sx130x_field_force_write(priv, F_RADIO_RST, 0);
	if (ret) {
		dev_err(dev, "radio deassert reset failed (%d)\n", ret);
		return ret;
	}

	/* radio */

	ret = devm_sx130x_register_radio_devices(dev);
	if (ret)
		return ret;

	mutex_lock(&priv->io_lock);

	/* GPIO */

	ret = regmap_read(priv->regmap, SX1301_GPMODE, &val);
	if (ret) {
		dev_err(dev, "GPIO mode read failed (%d)\n", ret);
		goto out;
	}

	val |= GENMASK(4, 0);

	ret = regmap_write(priv->regmap, SX1301_GPMODE, val);
	if (ret) {
		dev_err(dev, "GPIO mode write failed (%d)\n", ret);
		goto out;
	}

	ret = regmap_read(priv->regmap, SX1301_GPSO, &val);
	if (ret) {
		dev_err(dev, "GPIO select output read failed (%d)\n", ret);
		goto out;
	}

	val &= ~GENMASK(3, 0);
	val |= 2;

	ret = regmap_write(priv->regmap, SX1301_GPSO, val);
	if (ret) {
		dev_err(dev, "GPIO select output write failed (%d)\n", ret);
		goto out;
	}

	/* TODO LBT */

	ret = register_loradev(netdev);
	if (ret)
		goto out;

	dev_info(dev, "SX1301 module probed\n");

out:
	mutex_unlock(&priv->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(sx130x_probe);

int sx130x_remove(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);

	unregister_loradev(netdev);

	dev_info(dev, "SX1301 module removed\n");

	return 0;
}
EXPORT_SYMBOL_GPL(sx130x_remove);

static int sx130x_spi_probe(struct spi_device *spi)
{
	struct gpio_desc *rst;
	struct regmap *regmap;
	int ret;

	rst = devm_gpiod_get_optional(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(rst)) {
		if (PTR_ERR(rst) != -EPROBE_DEFER)
			dev_err(&spi->dev, "Failed to obtain reset GPIO\n");
		return PTR_ERR(rst);
	}

	gpiod_set_value_cansleep(rst, 1);
	msleep(100);
	gpiod_set_value_cansleep(rst, 0);
	msleep(100);

	spi->bits_per_word = 8;
	spi_setup(spi);

	regmap = devm_regmap_init_spi(spi, &sx130x_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&spi->dev, "Regmap allocation failed: %d\n", ret);
		return ret;
	}

	ret = sx130x_early_probe(regmap, rst);
	if (ret)
		return ret;

	return sx130x_probe(&spi->dev);
}

static int sx130x_spi_remove(struct spi_device *spi)
{
	return sx130x_remove(&spi->dev);;
}

#ifdef CONFIG_OF
static const struct of_device_id sx130x_dt_ids[] = {
	{ .compatible = "semtech,sx1301" },
	{}
};
MODULE_DEVICE_TABLE(of, sx130x_dt_ids);
#endif

static struct spi_driver sx130x_spi_driver = {
	.driver = {
		.name = "sx130x",
		.of_match_table = of_match_ptr(sx130x_dt_ids),
	},
	.probe = sx130x_spi_probe,
	.remove = sx130x_spi_remove,
};

static int __init sx130x_init(void)
{
	int ret;

	ret = sx130x_radio_init();
	if (ret)
		return ret;

	return spi_register_driver(&sx130x_spi_driver);
}
module_init(sx130x_init);

static void __exit sx130x_exit(void)
{
	spi_unregister_driver(&sx130x_spi_driver);
	sx130x_radio_exit();
}
module_exit(sx130x_exit);

MODULE_DESCRIPTION("SX130x SPI driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_AUTHOR("Ben Whitten <ben.whitten@gmail.com>");
MODULE_LICENSE("GPL");
