// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nordic nRF24L01+ transceiver
 *
 * Copyright (c) 2019 Andreas Färber
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

static int nrf24l01p_command(struct spi_device *spi, u8 cmd, u8 *data, int data_len)
{
	return spi_write_then_read(spi, &cmd, 1, data, data_len);
}

static int nrf24l01p_read_reg(struct spi_device *spi, u8 reg, u8 *data, int data_len)
{
	u8 cmd = (0 << 5) | (reg & 0x1f);

	return nrf24l01p_command(spi, cmd, data, data_len);
}

#ifdef CONFIG_OF
static const struct of_device_id nrf24l01p_dt_ids[] = {
	{ .compatible = "nordic,nrf24l01+" },
	{}
};
MODULE_DEVICE_TABLE(of, nrf24l01p_dt_ids);
#endif

static int nrf24l01p_probe(struct spi_device *spi)
{
	u8 data;
	int ret;

	ret = nrf24l01p_read_reg(spi, 0, &data, 1);
	if (ret)
		dev_err(&spi->dev, "spi error (%d)\n", ret);

	dev_info(&spi->dev, "config = 0x%02x\n", (unsigned int)data);

	dev_info(&spi->dev, "probed\n");

	return 0;
}

static int nrf24l01p_remove(struct spi_device *spi)
{
	dev_info(&spi->dev, "removed\n");

	return 0;
}

static struct spi_driver nrf24l01p_spi_driver = {
	.driver = {
		.name = "nrf24l01p",
		.of_match_table = of_match_ptr(nrf24l01p_dt_ids),
	},
	.probe = nrf24l01p_probe,
	.remove = nrf24l01p_remove,
};

module_spi_driver(nrf24l01p_spi_driver);

MODULE_DESCRIPTION("nRF24L01+ SPI driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
