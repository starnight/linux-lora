// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Semtech SX1272/SX1276 LoRa transceiver
 *
 * Copyright (c) 2016-2018 Andreas Färber
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/lora.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/lora/dev.h>
#include <linux/spi/spi.h>

#define REG_FIFO			0x00
#define REG_OPMODE			0x01
#define REG_FRF_MSB			0x06
#define REG_FRF_MID			0x07
#define REG_FRF_LSB			0x08
#define REG_PA_CONFIG			0x09
#define LORA_REG_FIFO_ADDR_PTR		0x0d
#define LORA_REG_FIFO_TX_BASE_ADDR	0x0e
#define LORA_REG_IRQ_FLAGS_MASK		0x11
#define LORA_REG_IRQ_FLAGS		0x12
#define LORA_REG_PAYLOAD_LENGTH		0x22
#define LORA_REG_SYNC_WORD		0x39
#define REG_DIO_MAPPING1		0x40
#define REG_DIO_MAPPING2		0x41
#define REG_VERSION			0x42
#define REG_PA_DAC			0x4d

#define REG_OPMODE_LONG_RANGE_MODE		BIT(7)
#define REG_OPMODE_LOW_FREQUENCY_MODE_ON	BIT(3)
#define REG_OPMODE_MODE_MASK			GENMASK(2, 0)
#define REG_OPMODE_MODE_SLEEP			(0x0 << 0)
#define REG_OPMODE_MODE_STDBY			(0x1 << 0)
#define REG_OPMODE_MODE_TX			(0x3 << 0)
#define REG_OPMODE_MODE_RXCONTINUOUS		(0x5 << 0)
#define REG_OPMODE_MODE_RXSINGLE		(0x6 << 0)

#define REG_PA_CONFIG_PA_SELECT			BIT(7)

#define LORA_REG_IRQ_FLAGS_TX_DONE		BIT(3)

#define REG_DIO_MAPPING1_DIO0_MASK	GENMASK(7, 6)

struct sx127x_priv;

struct sx127x_model {
	unsigned int number;
	unsigned int version;
	int (*reset)(struct sx127x_priv *priv);
};

struct sx127x_priv {
	struct lora_dev_priv lora;
	struct spi_device *spi;
	struct regmap *regmap;
	struct gpio_desc *rst;
	struct gpio_desc *dio[6];

	size_t fifosize;

	struct mutex spi_lock;

	struct sk_buff *tx_skb;
	int tx_len;

	struct workqueue_struct *wq;
	struct work_struct tx_work;
};

static bool sx127x_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == REG_FIFO)
		return true;

	return false;
}

static bool sx127x_writeable_noinc_reg(struct device *dev, unsigned int reg)
{
	if (reg == REG_FIFO)
		return true;

	return false;
}

static bool sx127x_readable_noinc_reg(struct device *dev, unsigned int reg)
{
	if (reg == REG_FIFO)
		return true;

	return false;
}

static struct regmap_config sx127x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.cache_type = REGCACHE_NONE,

	.read_flag_mask = 0,
	.write_flag_mask = BIT(7),

	.volatile_reg = sx127x_volatile_reg,
	.writeable_noinc_reg = sx127x_writeable_noinc_reg,
	.readable_noinc_reg = sx127x_readable_noinc_reg,

	.max_register = 0xff,
};

static netdev_tx_t sx127x_loradev_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct sx127x_priv *priv = netdev_priv(netdev);

	netdev_dbg(netdev, "%s\n", __func__);

	if (priv->tx_skb || priv->tx_len) {
		netdev_warn(netdev, "TX busy\n");
		return NETDEV_TX_BUSY;
	}

	if (skb->protocol != htons(ETH_P_LORA)) {
		kfree_skb(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	netif_stop_queue(netdev);
	priv->tx_skb = skb;
	queue_work(priv->wq, &priv->tx_work);

	return NETDEV_TX_OK;
}

static int sx127x_tx(struct spi_device *spi, void *data, int data_len)
{
	struct net_device *netdev = spi_get_drvdata(spi);
	struct sx127x_priv *priv = netdev_priv(netdev);
	unsigned int addr, val;
	int ret;

	dev_dbg(&spi->dev, "%s (data_len %d)\n", __func__, data_len);

	ret = regmap_read(priv->regmap, REG_OPMODE, &val);
	if (ret) {
		dev_err(&spi->dev, "Failed to read RegOpMode (%d)\n", ret);
		return ret;
	}
	dev_dbg(&spi->dev, "RegOpMode = 0x%02x\n", val);
	if (!(val & REG_OPMODE_LONG_RANGE_MODE))
		dev_err(&spi->dev, "LongRange Mode not active!\n");
	if ((val & REG_OPMODE_MODE_MASK) == REG_OPMODE_MODE_SLEEP)
		dev_err(&spi->dev, "Cannot access FIFO in Sleep Mode!\n");

	ret = regmap_read(priv->regmap, LORA_REG_FIFO_TX_BASE_ADDR, &addr);
	if (ret) {
		dev_err(&spi->dev, "Failed to read RegFifoTxBaseAddr (%d)\n", ret);
		return ret;
	}
	dev_dbg(&spi->dev, "RegFifoTxBaseAddr = 0x%02x\n", addr);

	ret = regmap_write(priv->regmap, LORA_REG_FIFO_ADDR_PTR, addr);
	if (ret) {
		dev_err(&spi->dev, "Failed to write RegFifoAddrPtr (%d)\n", ret);
		return ret;
	}

	ret = regmap_write(priv->regmap, LORA_REG_PAYLOAD_LENGTH, data_len);
	if (ret) {
		dev_err(&spi->dev, "Failed to write RegPayloadLength (%d)\n", ret);
		return ret;
	}

	ret = regmap_noinc_write(priv->regmap, REG_FIFO, data, data_len);
	if (ret) {
		dev_err(&spi->dev, "Failed to write into FIFO (%d)\n", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, LORA_REG_IRQ_FLAGS, &val);
	if (ret) {
		dev_err(&spi->dev, "Failed to read RegIrqFlags (%d)\n", ret);
		return ret;
	}
	dev_dbg(&spi->dev, "RegIrqFlags = 0x%02x\n", val);

	ret = regmap_write(priv->regmap, LORA_REG_IRQ_FLAGS, LORA_REG_IRQ_FLAGS_TX_DONE);
	if (ret) {
		dev_err(&spi->dev, "Failed to write RegIrqFlags (%d)\n", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, LORA_REG_IRQ_FLAGS_MASK, &val);
	if (ret) {
		dev_err(&spi->dev, "Failed to read RegIrqFlagsMask (%d)\n", ret);
		return ret;
	}
	dev_dbg(&spi->dev, "RegIrqFlagsMask = 0x%02x\n", val);

	val &= ~LORA_REG_IRQ_FLAGS_TX_DONE;
	ret = regmap_write(priv->regmap, LORA_REG_IRQ_FLAGS_MASK, val);
	if (ret) {
		dev_err(&spi->dev, "Failed to write RegIrqFlagsMask (%d)\n", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, REG_DIO_MAPPING1, &val);
	if (ret) {
		dev_err(&spi->dev, "Failed to read RegDioMapping1 (%d)\n", ret);
		return ret;
	}

	val &= ~REG_DIO_MAPPING1_DIO0_MASK;
	val |= 0x1 << 6;
	ret = regmap_write(priv->regmap, REG_DIO_MAPPING1, val);
	if (ret) {
		dev_err(&spi->dev, "Failed to write RegDioMapping1 (%d)\n", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, REG_OPMODE, &val);
	if (ret) {
		dev_err(&spi->dev, "Failed to read RegOpMode (%d)\n", ret);
		return ret;
	}

	val &= ~REG_OPMODE_MODE_MASK;
	val |= REG_OPMODE_MODE_TX;
	ret = regmap_write(priv->regmap, REG_OPMODE, val);
	if (ret) {
		dev_err(&spi->dev, "Failed to write RegOpMode (%d)\n", ret);
		return ret;
	}

	dev_dbg(&spi->dev, "%s: done\n", __func__);

	return 0;
}

static void sx127x_tx_work_handler(struct work_struct *ws)
{
	struct sx127x_priv *priv = container_of(ws, struct sx127x_priv, tx_work);
	struct spi_device *spi = priv->spi;
	struct net_device *netdev = spi_get_drvdata(spi);

	netdev_dbg(netdev, "%s\n", __func__);

	mutex_lock(&priv->spi_lock);

	if (priv->tx_skb) {
		sx127x_tx(spi, priv->tx_skb->data, priv->tx_skb->len);
		priv->tx_len = 1 + priv->tx_skb->len;
		if (!(netdev->flags & IFF_ECHO) ||
			priv->tx_skb->pkt_type != PACKET_LOOPBACK ||
			priv->tx_skb->protocol != htons(ETH_P_LORA))
			kfree_skb(priv->tx_skb);
		priv->tx_skb = NULL;
	}

	mutex_unlock(&priv->spi_lock);
}

static irqreturn_t sx127x_dio_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct sx127x_priv *priv = netdev_priv(netdev);
	unsigned int val;
	int ret;

	netdev_dbg(netdev, "%s\n", __func__);

	mutex_lock(&priv->spi_lock);

	ret = regmap_read(priv->regmap, LORA_REG_IRQ_FLAGS, &val);
	if (ret) {
		netdev_warn(netdev, "Failed to read RegIrqFlags (%d)\n", ret);
		val = 0;
	}

	if (val & LORA_REG_IRQ_FLAGS_TX_DONE) {
		netdev_info(netdev, "TX done.\n");
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += priv->tx_len - 1;
		priv->tx_len = 0;
		netif_wake_queue(netdev);

		ret = regmap_write(priv->regmap, LORA_REG_IRQ_FLAGS, LORA_REG_IRQ_FLAGS_TX_DONE);
		if (ret)
			netdev_warn(netdev, "Failed to write RegIrqFlags (%d)\n", ret);
	}

	mutex_unlock(&priv->spi_lock);

	return IRQ_HANDLED;
}

static int sx127x_loradev_open(struct net_device *netdev)
{
	struct sx127x_priv *priv = netdev_priv(netdev);
	unsigned int val;
	int ret, irq;

	netdev_dbg(netdev, "%s\n", __func__);

	ret = open_loradev(netdev);
	if (ret)
		return ret;

	mutex_lock(&priv->spi_lock);

	ret = regmap_read(priv->regmap, REG_OPMODE, &val);
	if (ret) {
		netdev_err(netdev, "Failed to read RegOpMode (%d)\n", ret);
		goto err_opmode;
	}

	val &= ~REG_OPMODE_MODE_MASK;
	val |= REG_OPMODE_MODE_STDBY;
	ret = regmap_write(priv->regmap, REG_OPMODE, val);
	if (ret) {
		netdev_err(netdev, "Failed to write RegOpMode (%d)\n", ret);
		goto err_opmode;
	}

	priv->tx_skb = NULL;
	priv->tx_len = 0;

	priv->wq = alloc_workqueue("sx127x_wq", WQ_FREEZABLE | WQ_MEM_RECLAIM, 0);
	INIT_WORK(&priv->tx_work, sx127x_tx_work_handler);

	if (priv->dio[0]) {
		irq = gpiod_to_irq(priv->dio[0]);
		if (irq <= 0)
			netdev_warn(netdev, "Failed to obtain interrupt for DIO0 (%d)\n", irq);
		else {
			netdev_info(netdev, "Succeeded in obtaining interrupt for DIO0: %d\n", irq);
			ret = request_threaded_irq(irq, NULL, sx127x_dio_interrupt, IRQF_ONESHOT | IRQF_TRIGGER_RISING, netdev->name, netdev);
			if (ret) {
				netdev_err(netdev, "Failed to request interrupt for DIO0 (%d)\n", ret);
				goto err_irq;
			}
		}
	}

	netif_wake_queue(netdev);

	mutex_unlock(&priv->spi_lock);

	return 0;

err_irq:
	destroy_workqueue(priv->wq);
	priv->wq = NULL;
err_opmode:
	close_loradev(netdev);
	mutex_unlock(&priv->spi_lock);
	return ret;
}

static int sx127x_loradev_stop(struct net_device *netdev)
{
	struct sx127x_priv *priv = netdev_priv(netdev);
	unsigned int val;
	int ret, irq;

	netdev_dbg(netdev, "%s\n", __func__);

	close_loradev(netdev);

	mutex_lock(&priv->spi_lock);

	ret = regmap_write(priv->regmap, LORA_REG_IRQ_FLAGS_MASK, 0xff);
	if (ret) {
		netdev_err(netdev, "Failed to write RegIrqFlagsMask (%d)\n", ret);
		goto err_irqmask;
	}

	ret = regmap_read(priv->regmap, REG_OPMODE, &val);
	if (ret) {
		netdev_err(netdev, "Failed to read RegOpMode (%d)\n", ret);
		goto err_opmode;
	}

	val &= ~REG_OPMODE_MODE_MASK;
	val |= REG_OPMODE_MODE_SLEEP;
	ret = regmap_write(priv->regmap, REG_OPMODE, val);
	if (ret) {
		netdev_err(netdev, "Failed to write RegOpMode (%d)\n", ret);
		goto err_opmode;
	}

	if (priv->dio[0]) {
		irq = gpiod_to_irq(priv->dio[0]);
		if (irq > 0) {
			netdev_dbg(netdev, "Freeing IRQ %d\n", irq);
			free_irq(irq, netdev);
		}
	}

	destroy_workqueue(priv->wq);
	priv->wq = NULL;

	if (priv->tx_skb || priv->tx_len)
		netdev->stats.tx_errors++;
	if (priv->tx_skb)
		dev_kfree_skb(priv->tx_skb);
	priv->tx_skb = NULL;
	priv->tx_len = 0;

	mutex_unlock(&priv->spi_lock);

	return 0;

err_opmode:
err_irqmask:
	mutex_unlock(&priv->spi_lock);
	return ret;
}

static const struct net_device_ops sx127x_netdev_ops =  {
	.ndo_open = sx127x_loradev_open,
	.ndo_stop = sx127x_loradev_stop,
	.ndo_start_xmit = sx127x_loradev_start_xmit,
};

static int sx1272_reset(struct sx127x_priv *priv)
{
	if (!priv->rst)
		return 0;

	gpiod_set_value_cansleep(priv->rst, 0);
	udelay(100);

	gpiod_set_value_cansleep(priv->rst, 1);
	msleep(5);

	return 0;
}

static int sx1276_reset(struct sx127x_priv *priv)
{
	if (!priv->rst)
		return 0;

	gpiod_set_value_cansleep(priv->rst, 1);
	udelay(100);

	gpiod_set_value_cansleep(priv->rst, 0);
	msleep(5);

	return 0;
}

static struct sx127x_model sx1272_model = {
	.number = 1272,
	.version = 0x22,
	.reset = sx1272_reset,
};

static struct sx127x_model sx1276_model = {
	.number = 1276,
	.version = 0x12,
	.reset = sx1276_reset,
};

#ifdef CONFIG_OF
static const struct of_device_id sx127x_dt_ids[] = {
	{ .compatible = "semtech,sx1272", .data = &sx1272_model },
	{ .compatible = "semtech,sx1276", .data = &sx1276_model },
	{}
};
MODULE_DEVICE_TABLE(of, sx127x_dt_ids);
#endif

static int sx127x_probe(struct spi_device *spi)
{
	struct net_device *netdev;
	struct sx127x_priv *priv;
	const struct sx127x_model *model = NULL;
	u32 freq_xosc, freq_band;
	unsigned long long freq_rf;
	unsigned int val;
	int ret, i;

	netdev = devm_alloc_loradev(&spi->dev, sizeof(struct sx127x_priv));
	if (!netdev)
		return -ENOMEM;

	netdev->netdev_ops = &sx127x_netdev_ops;
	netdev->flags |= IFF_ECHO;

	priv = netdev_priv(netdev);
	priv->spi = spi;
	mutex_init(&priv->spi_lock);

	priv->regmap = devm_regmap_init_spi(spi, &sx127x_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&spi->dev, "regmap allocation failed (%d)\n", ret);
		return ret;
	}

	priv->rst = devm_gpiod_get_optional(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (priv->rst == NULL)
		dev_warn(&spi->dev, "no reset GPIO available, ignoring");

	for (i = 0; i < 6; i++) {
		priv->dio[i] = devm_gpiod_get_index_optional(&spi->dev, "dio", i, GPIOD_IN);
		if (priv->dio[i] == NULL)
			dev_dbg(&spi->dev, "DIO%d not available, ignoring", i);
	}

	spi->bits_per_word = 8;
	spi_setup(spi);

	model = of_device_get_match_data(&spi->dev);
	if (model) {
		ret = model->reset(priv);
		if (ret) {
			dev_err(&spi->dev, "reset failed (%d)\n", ret);
			return ret;
		}

		ret = regmap_read(priv->regmap, REG_VERSION, &val);
		if (ret) {
			dev_err(&spi->dev, "version read failed\n");
			return ret;
		}
		if (val != model->version) {
			dev_err(&spi->dev, "unexpected version read: 0x%x\n", val);
			return -EINVAL;
		}
	} else {
		dev_info(&spi->dev, "auto-detecting model\n");
		ret = sx1272_reset(priv);
		if (ret) {
			dev_err(&spi->dev, "SX1272 reset failed (%d)\n", ret);
			return ret;
		}

		ret = regmap_read(priv->regmap, REG_VERSION, &val);
		if (ret) {
			dev_err(&spi->dev, "version read failed\n");
			return ret;
		}

		if (val == sx1272_model.version)
			model = &sx1272_model;
		else {
			ret = sx1276_reset(priv);
			if (ret) {
				dev_err(&spi->dev, "SX1276 reset failed (%d)\n", ret);
				return ret;
			}

			ret = regmap_read(priv->regmap, REG_VERSION, &val);
			if (ret) {
				dev_err(&spi->dev, "version read failed\n");
				return ret;
			}

			if (val == sx1276_model.version)
				model = &sx1276_model;
			else {
				dev_err(&spi->dev, "transceiver not recognized (RegVersion = 0x%02x)\n", val);
				return -EINVAL;
			}
		}
	}

	ret = of_property_read_u32(spi->dev.of_node, "clock-frequency", &freq_xosc);
	if (ret) {
		dev_err(&spi->dev, "failed reading clock-frequency");
		return ret;
	}

	ret = of_property_read_u32(spi->dev.of_node, "radio-frequency", &freq_band);
	if (ret) {
		dev_err(&spi->dev, "failed reading radio-frequency");
		return ret;
	}

	val = REG_OPMODE_LONG_RANGE_MODE | REG_OPMODE_MODE_SLEEP;
	if (freq_band < 525000000)
		val |= REG_OPMODE_LOW_FREQUENCY_MODE_ON;
	ret = regmap_write(priv->regmap, REG_OPMODE, val);
	if (ret) {
		dev_err(&spi->dev, "failed writing opmode");
		return ret;
	}

	freq_rf = freq_band;
	freq_rf *= (1 << 19);
	do_div(freq_rf, freq_xosc);
	dev_dbg(&spi->dev, "Frf = %llu", freq_rf);

	ret = regmap_write(priv->regmap, REG_FRF_MSB, freq_rf >> 16);
	if (!ret)
		ret = regmap_write(priv->regmap, REG_FRF_MID, freq_rf >> 8);
	if (!ret)
		ret = regmap_write(priv->regmap, REG_FRF_LSB, freq_rf);
	if (ret) {
		dev_err(&spi->dev, "failed writing frequency (%d)", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, REG_PA_CONFIG, &val);
	if (ret) {
		dev_err(&spi->dev, "failed reading RegPaConfig\n");
		return ret;
	}
	if (true)
		val |= REG_PA_CONFIG_PA_SELECT;
	val &= ~GENMASK(3, 0);
	val |= (23 - 3) - 5;
	ret = regmap_write(priv->regmap, REG_PA_CONFIG, val);
	if (ret) {
		dev_err(&spi->dev, "failed writing RegPaConfig\n");
		return ret;
	}

	ret = regmap_read(priv->regmap, REG_PA_DAC, &val);
	if (ret) {
		dev_err(&spi->dev, "failed reading RegPaDac\n");
		return ret;
	}
	val &= ~GENMASK(2, 0);
	val |= 0x7;
	ret = regmap_write(priv->regmap, REG_PA_DAC, val);
	if (ret) {
		dev_err(&spi->dev, "failed writing RegPaDac\n");
		return ret;
	}

	spi_set_drvdata(spi, netdev);
	SET_NETDEV_DEV(netdev, &spi->dev);

	ret = register_loradev(netdev);
	if (ret)
		return ret;

	dev_info(&spi->dev, "probed (SX%d)\n", model->number);

	return 0;
}

static int sx127x_remove(struct spi_device *spi)
{
	struct net_device *netdev = spi_get_drvdata(spi);

	unregister_loradev(netdev);

	dev_info(&spi->dev, "removed\n");

	return 0;
}

static struct spi_driver sx127x_spi_driver = {
	.driver = {
		.name = "sx127x",
		.of_match_table = of_match_ptr(sx127x_dt_ids),
	},
	.probe = sx127x_probe,
	.remove = sx127x_remove,
};

module_spi_driver(sx127x_spi_driver);

MODULE_DESCRIPTION("SX127x SPI driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
