// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SiLabs Si4432 transceiver
 *
 * Copyright (c) 2019 Andreas Färber
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

static struct regmap_config si443x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_NONE,

	.read_flag_mask = 0,
	.write_flag_mask = BIT(7),

	.max_register = 0x7f,
};

#ifdef CONFIG_OF
static const struct of_device_id si443x_dt_ids[] = {
	{ .compatible = "silabs,si4432" },
	{}
};
MODULE_DEVICE_TABLE(of, si443x_dt_ids);
#endif

static int si443x_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	unsigned int val;
	int ret;

	spi->bits_per_word = 8;
	spi_setup(spi);

	regmap = devm_regmap_init_spi(spi, &si443x_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		return ret;
	}

	ret = regmap_read(regmap, 0, &val);
	if (ret)
		return ret;

	dev_info(&spi->dev, "device type: 0x%02x\n", val);

	ret = regmap_read(regmap, 1, &val);
	if (ret)
		return ret;

	dev_info(&spi->dev, "device version: 0x%02x\n", val);

	dev_info(&spi->dev, "probed\n");

	return 0;
}

static int si443x_remove(struct spi_device *spi)
{
	dev_info(&spi->dev, "removed\n");

	return 0;
}

static struct spi_driver si443x_spi_driver = {
	.driver = {
		.name = "si443x",
		.of_match_table = of_match_ptr(si443x_dt_ids),
	},
	.probe = si443x_probe,
	.remove = si443x_remove,
};

module_spi_driver(si443x_spi_driver);

MODULE_DESCRIPTION("Si443x SPI driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
