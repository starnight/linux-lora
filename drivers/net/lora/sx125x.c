// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Semtech SX1255/SX1257 LoRa transceiver
 *
 * Copyright (c) 2018 Andreas Färber
 * Copyright (c) 2018 Ben Whitten
 *
 * Based on SX1301 HAL code:
 * Copyright (c) 2013 Semtech-Cycleo
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#ifdef CONFIG_LORA_SX125X_SPI
#include <linux/spi/spi.h>
#endif

#ifdef CONFIG_LORA_SX125X_CON
#include <linux/lora/sx130x.h>
#endif

#include "sx125x.h"

enum sx125x_fields {
	F_CLK_OUT,
	F_TX_DAC_CLK_SEL,
	F_SX1257_XOSC_GM_STARTUP,
	F_SX1257_XOSC_DISABLE_CORE,
};

static const struct reg_field sx125x_regmap_fields[] = {
	/* CLK_SELECT */
	[F_CLK_OUT]        = REG_FIELD(SX125X_CLK_SELECT, 1, 1),
	[F_TX_DAC_CLK_SEL] = REG_FIELD(SX125X_CLK_SELECT, 0, 0),
	/* XOSC */ /* TODO maybe make this dynamic */
	[F_SX1257_XOSC_GM_STARTUP]  = REG_FIELD(SX1257_XOSC, 0, 3),
	[F_SX1257_XOSC_DISABLE_CORE]  = REG_FIELD(SX1257_XOSC, 5, 5),
};

struct sx125x_priv {
	struct regmap		*regmap;
	struct regmap_field     *regmap_fields[ARRAY_SIZE(sx125x_regmap_fields)];
};

static struct regmap_config __maybe_unused sx125x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_NONE,

	.read_flag_mask = 0,
	.write_flag_mask = BIT(7),

	.max_register = SX125X_MAX_REGISTER,
};

static int sx125x_field_write(struct sx125x_priv *priv,
		enum sx125x_fields field_id, u8 val)
{
	return regmap_field_write(priv->regmap_fields[field_id], val);
}

static int __maybe_unused sx125x_regmap_probe(struct device *dev, struct regmap *regmap)
{
	struct sx125x_priv *priv;
	unsigned int val;
	int ret;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);
	priv->regmap = regmap;
	for (i = 0; i < ARRAY_SIZE(sx125x_regmap_fields); i++) {
		const struct reg_field *reg_fields = sx125x_regmap_fields;

		priv->regmap_fields[i] = devm_regmap_field_alloc(dev,
				priv->regmap,
				reg_fields[i]);
		if (IS_ERR(priv->regmap_fields[i])) {
			ret = PTR_ERR(priv->regmap_fields[i]);
			dev_err(dev, "Cannot allocate regmap field: %d\n", ret);
			return ret;
		}
	}

	if (true) {
		ret = regmap_read(priv->regmap, SX1255_VERSION, &val);
		if (ret) {
			dev_err(dev, "version read failed\n");
			return ret;
		}
		dev_info(dev, "SX125x version: %02x\n", val);
	}

	if (false) {
		ret = sx125x_field_write(priv, F_CLK_OUT, 1);
		if (ret) {
			dev_err(dev, "enabling clock output failed\n");
			return ret;
		}

		dev_info(dev, "enabling clock output\n");
	}

	/* TODO Only needs setting on radio on the TX path */
	ret = sx125x_field_write(priv, F_TX_DAC_CLK_SEL, 1);
	if (ret) {
		dev_err(dev, "clock select failed\n");
		return ret;
	}

	dev_dbg(dev, "clk written\n");

	if (true) {
		ret = sx125x_field_write(priv, F_SX1257_XOSC_DISABLE_CORE, 1);
		if (ret) {
			dev_err(dev, "xosc disable failed\n");
			return ret;
		}

		ret = sx125x_field_write(priv, F_SX1257_XOSC_GM_STARTUP, 13);
		if (ret) {
			dev_err(dev, "xosc startup adjust failed\n");
			return ret;
		}
	}

	dev_info(dev, "SX125x module probed\n");

	return 0;
}

static int __maybe_unused sx125x_regmap_remove(struct device *dev)
{
	dev_info(dev, "SX125x module removed\n");

	return 0;
}

#ifdef CONFIG_LORA_SX125X_CON
static int sx125x_con_probe(struct sx130x_radio_device *rdev)
{
	struct device *dev = &rdev->dev;
	int ret;

	rdev->regmap = devm_regmap_init(dev, rdev->regmap_bus, rdev,
			&sx125x_regmap_config);
	if (IS_ERR(rdev->regmap)) {
		ret = PTR_ERR(rdev->regmap);
		dev_err(dev, "Regmap allocation failed: %d\n", ret);
		return ret;
	}

	return sx125x_regmap_probe(dev, rdev->regmap);
}

static int sx125x_con_remove(struct sx130x_radio_device *rdev)
{
	return sx125x_regmap_remove(&rdev->dev);
}

#ifdef CONFIG_OF
static const struct of_device_id sx125x_con_of_match[] = {
	{ .compatible = "semtech,sx1255" },
	{ .compatible = "semtech,sx1257" },
	{},
};
MODULE_DEVICE_TABLE(of, sx125x_con_of_match);
#endif

static struct sx130x_radio_driver sx125x_con_driver = {
	.probe  = sx125x_con_probe,
	.remove = sx125x_con_remove,
	.driver = {
		.name = "sx125x_con",
		.of_match_table = of_match_ptr(sx125x_con_of_match),
	},
};
#endif

#ifdef CONFIG_LORA_SX125X_SPI
static int sx125x_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	int ret;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->max_speed_hz = 10000000;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "SPI setup failed.\n");
		return ret;
	}

	regmap = devm_regmap_init_spi(spi, &sx125x_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&spi->dev, "Regmap allocation failed: %d\n", ret);
		return ret;
	}

	return sx125x_regmap_probe(&spi->dev, regmap);
}

static int sx125x_spi_remove(struct spi_device *spi)
{
	return sx125x_regmap_remove(&spi->dev);
}

#ifdef CONFIG_OF
static const struct of_device_id sx125x_spi_of_match[] = {
	{ .compatible = "semtech,sx1255" },
	{ .compatible = "semtech,sx1257" },
	{},
};
MODULE_DEVICE_TABLE(of, sx125x_spi_of_match);
#endif

static struct spi_driver sx125x_spi_driver = {
	.probe  = sx125x_spi_probe,
	.remove = sx125x_spi_remove,
	.driver = {
		.name = "sx125x_spi",
		.of_match_table = of_match_ptr(sx125x_spi_of_match),
	},
};
#endif

static int __init sx125x_init(void)
{
	int ret = 0;

#ifdef CONFIG_LORA_SX125X_SPI
	ret = spi_register_driver(&sx125x_spi_driver);
	if (ret < 0) {
		pr_err("failed to init sx125x spi (%d)\n", ret);
		return ret;
	}
#endif
#ifdef CONFIG_LORA_SX125X_CON
	ret = sx130x_register_radio_driver(&sx125x_con_driver);
	if (ret < 0) {
		pr_err("failed to init sx125x con (%d)\n", ret);
		return ret;
	}
#endif

	return ret;
}
module_init(sx125x_init);

static void __exit sx125x_exit(void)
{
#ifdef CONFIG_LORA_SX125X_SPI
	spi_unregister_driver(&sx125x_spi_driver);
#endif
#ifdef CONFIG_LORA_SX125X_CON
	sx130x_unregister_radio_driver(&sx125x_con_driver);
#endif
}
module_exit(sx125x_exit);

MODULE_DESCRIPTION("Semtech SX125x LoRa Radio Driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_AUTHOR("Ben Whitten <ben.whitten@gmail.com>");
MODULE_LICENSE("GPL");
