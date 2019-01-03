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

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/lora/sx130x.h>

#include "sx130x.h"

struct sx130x_radio_regs {
	unsigned int addr, data, cs, rb;
};

#define SX130X_RADIO_REGS(a) \
	{ \
		.addr = SX1301_RADIO_ ## a ## _SPI_ADDR, \
		.data = SX1301_RADIO_ ## a ## _SPI_DATA, \
		.cs = SX1301_RADIO_ ## a ## _SPI_CS, \
		.rb = SX1301_RADIO_ ## a ## _SPI_DATA_RB, \
	}

static const struct sx130x_radio_regs sx130x_radio_regs[2] = {
	SX130X_RADIO_REGS(A),
	SX130X_RADIO_REGS(B)
};

static inline const struct sx130x_radio_regs *sx130x_get_radio_regs(unsigned int radio)
{
	return &sx130x_radio_regs[radio % ARRAY_SIZE(sx130x_radio_regs)];
}

static int sx130x_radio_write_one(struct regmap *regmap,
	const struct sx130x_radio_regs *regs,
	u8 addr, u8 val)
{
	struct device *dev = regmap_get_device(regmap);
	int ret;

	sx130x_io_lock(dev);

	ret = regmap_write(regmap, regs->cs, 0);
	if (ret)
		goto out;

	ret = regmap_raw_write(regmap, regs->addr, &addr, 1);
	if (ret)
		goto out;

	ret = regmap_raw_write(regmap, regs->data, &val, 1);
	if (ret)
		goto out;

	ret = regmap_write(regmap, regs->cs, 1);
	if (ret)
		goto out;

	ret = regmap_write(regmap, regs->cs, 0);
out:
	sx130x_io_unlock(dev);
	return ret;
}

static int sx130x_radio_read_one(struct regmap *regmap,
	const struct sx130x_radio_regs *regs,
	u8 addr, u8 *val)
{
	struct device *dev = regmap_get_device(regmap);
	int ret;

	sx130x_io_lock(dev);

	ret = regmap_write(regmap, regs->cs, 0);
	if (ret)
		goto out;

	/* address to tx */
	ret = regmap_raw_write(regmap, regs->addr, &addr, 1);
	if (ret)
		goto out;

	ret = regmap_write(regmap, regs->data, 0);
	if (ret)
		goto out;

	ret = regmap_write(regmap, regs->cs, 1);
	if (ret)
		goto out;

	ret = regmap_write(regmap, regs->cs, 0);
	if (ret)
		goto out;

	/* reading */
	ret = regmap_raw_read(regmap, regs->rb, val, 1);
out:
	sx130x_io_unlock(dev);
	return ret;
}

static int sx130x_radio_gather_write(void *context,
	const void *reg_buf, size_t reg_size, const void *val_buf, size_t val_size)
{
	struct sx130x_radio_device *rdev = context;
	struct regmap *regmap = sx130x_get_regmap(rdev->concentrator);
	struct device *dev = rdev->concentrator;
	const u8 *reg = reg_buf;
	const u8 *val = val_buf;
	int ret;

	dev_dbg(dev, "%s: radio %c 0x%x (reg_size %zu) 0x%x (val_size %zu)\n", __func__, 'A' + rdev->nr, (unsigned int)*reg, reg_size, (unsigned int)*val, val_size);

	if (reg_size != 1 || val_size != 1)
		return -EINVAL;

	ret = sx130x_radio_write_one(regmap, sx130x_get_radio_regs(rdev->nr), *reg, *val);
	if (ret)
		return ret;

	dev_dbg(dev, "%s: done\n", __func__);
	return 0;
}

static int sx130x_radio_write(void *context,
	const void *data_buf, size_t count)
{
	struct sx130x_radio_device *rdev = context;
	struct device *dev = rdev->concentrator;
	const u8 *data = data_buf;

	dev_dbg(dev, "%s: radio %c 0x%02x 0x%02x (count %zu)\n", __func__, 'A' + rdev->nr, (unsigned int)data[0], (unsigned int)data[1], count);

	if (count != 2)
		return -EINVAL;

	return sx130x_radio_gather_write(context, data, 1, data + 1, 1);
}

static int sx130x_radio_read(void *context,
	const void *reg_buf, size_t reg_size, void *val_buf, size_t val_size)
{
	struct sx130x_radio_device *rdev = context;
	struct regmap *regmap = sx130x_get_regmap(rdev->concentrator);
	struct device *dev = rdev->concentrator;
	const u8 *reg = reg_buf;
	u8 *val = val_buf;
	int ret;

	dev_dbg(dev, "%s: radio %c 0x%x (reg_size %zu) (val_size %zu)\n", __func__, 'A' + rdev->nr, (unsigned int)*reg, reg_size, val_size);

	if (reg_size != 1 || val_size != 1)
		return -EINVAL;

	ret = sx130x_radio_read_one(regmap, sx130x_get_radio_regs(rdev->nr), *reg, val);
	if (ret)
		return ret;

	dev_dbg(dev, "%s: = 0x%x\n", __func__, (unsigned int)*val);
	return 0;
}

static const struct regmap_bus sx130x_radio_regmap_bus = {
	.write = sx130x_radio_write,
	.gather_write = sx130x_radio_gather_write,
	.read = sx130x_radio_read,

	.max_raw_read = 1,
	.max_raw_write = 1,
};

static int sx130x_radio_match_device(struct device *dev, struct device_driver *drv)
{
	return of_driver_match_device(dev, drv);
}

static struct bus_type sx130x_radio_bus_type = {
	.name = "sx130x_radio",
	.match = sx130x_radio_match_device,
	.uevent = of_device_uevent_modalias,
};

int __init sx130x_radio_init(void)
{
	int ret;

	ret = bus_register(&sx130x_radio_bus_type);
	if (ret < 0)
		return ret;

	return 0;
}

void __exit sx130x_radio_exit(void)
{
	bus_unregister(&sx130x_radio_bus_type);
}

static int sx130x_radio_drv_probe(struct device *dev)
{
	const struct sx130x_radio_driver *rdrv = to_sx130x_radio_driver(dev->driver);
	int ret;

	ret = rdrv->probe(to_sx130x_radio_device(dev));

	return ret;
}

static int sx130x_radio_drv_remove(struct device *dev)
{
	const struct sx130x_radio_driver *rdrv = to_sx130x_radio_driver(dev->driver);
	int ret;

	ret = rdrv->remove(to_sx130x_radio_device(dev));

	return ret;
}

int __sx130x_register_radio_driver(struct module *owner, struct sx130x_radio_driver *rdrv)
{
	rdrv->driver.owner = owner;
	rdrv->driver.bus = &sx130x_radio_bus_type;

	if (rdrv->probe)
		rdrv->driver.probe = sx130x_radio_drv_probe;
	if (rdrv->remove)
		rdrv->driver.remove = sx130x_radio_drv_remove;

	return driver_register(&rdrv->driver);
}
EXPORT_SYMBOL_GPL(__sx130x_register_radio_driver);

static void sx130x_radio_release(struct device *dev)
{
	struct sx130x_radio_device *radio = to_sx130x_radio_device(dev);

	put_device(radio->concentrator);
	kfree(radio);
}

static struct sx130x_radio_device *sx130x_alloc_radio_device(struct device *dev)
{
	struct sx130x_radio_device *radio;

	if (!get_device(dev))
		return NULL;

	radio = kzalloc(sizeof(*radio), GFP_KERNEL);
	if (!radio) {
		put_device(dev);
		return NULL;
	}

	radio->dev.parent = dev;
	radio->dev.bus = &sx130x_radio_bus_type;
	radio->dev.release = sx130x_radio_release;

	radio->concentrator = dev;
	radio->regmap_bus = &sx130x_radio_regmap_bus;

	device_initialize(&radio->dev);
	return radio;
}

static void sx130x_radio_dev_set_name(struct sx130x_radio_device *radio)
{
	dev_set_name(&radio->dev, "%s-%c", dev_name(radio->concentrator), 'a' + radio->nr);
}

static int sx130x_radio_dev_check(struct device *dev, void *data)
{
	struct sx130x_radio_device *radio = to_sx130x_radio_device(dev);
	struct sx130x_radio_device *new_radio = data;

	if (radio->concentrator == new_radio->concentrator &&
	    radio->nr == new_radio->nr)
		return -EBUSY;

	return 0;
}

static int sx130x_add_radio_device(struct sx130x_radio_device *radio)
{
	static DEFINE_MUTEX(sx130x_radio_add_lock);
	int ret;

	if (radio->nr >= 2)
		return -EINVAL;

	sx130x_radio_dev_set_name(radio);

	mutex_lock(&sx130x_radio_add_lock);

	ret = bus_for_each_dev(&sx130x_radio_bus_type, NULL, radio, sx130x_radio_dev_check);
	if (ret)
		goto done;

	ret = device_add(&radio->dev);
	if (ret < 0)
		dev_err(&radio->dev, "can't add %s (%d)\n", dev_name(&radio->dev), ret);

done:
	mutex_unlock(&sx130x_radio_add_lock);

	return ret;
}

static int sx130x_radio_parse_dt(struct device *dev, struct sx130x_radio_device *radio, struct device_node *node)
{
	u32 value;
	int ret;

	ret = of_property_read_u32(node, "reg", &value);
	if (ret) {
		dev_err(dev, "%pOF has no valid reg property (%d)\n", node, ret);
		return ret;
	}
	radio->nr = value;

	return 0;
}

static struct sx130x_radio_device *sx130x_register_radio_device(struct device *dev, struct device_node *node)
{
	struct sx130x_radio_device *radio;
	int ret;

	radio = sx130x_alloc_radio_device(dev);
	if (!radio)
		return ERR_PTR(-ENOMEM);

	ret = sx130x_radio_parse_dt(dev, radio, node);
	if (ret) {
		sx130x_radio_put(radio);
		return ERR_PTR(ret);
	}

	of_node_get(node);
	radio->dev.of_node = node;

	ret = sx130x_add_radio_device(radio);
	if (ret) {
		of_node_put(node);
		sx130x_radio_put(radio);
		return ERR_PTR(ret);
	}

	dev_dbg(dev, "added child %s\n", dev_name(&radio->dev));

	return radio;
}

static void sx130x_unregister_radio_device(struct sx130x_radio_device *radio)
{
	if (!radio)
		return;

	if (radio->dev.of_node) {
		of_node_clear_flag(radio->dev.of_node, OF_POPULATED);
		of_node_put(radio->dev.of_node);
	}
	device_unregister(&radio->dev);
}

int sx130x_register_radio_devices(struct device *dev)
{
	struct device_node *spi, *node;
	struct sx130x_radio_device *radio;
	unsigned int found = 0;

	spi = of_get_child_by_name(dev->of_node, "radio-spi");
	if (IS_ERR(spi))
		return PTR_ERR(spi);

	for_each_available_child_of_node(spi, node) {
		if (of_node_test_and_set_flag(node, OF_POPULATED))
			continue;

		radio = sx130x_register_radio_device(dev, node);
		if (IS_ERR(radio)) {
			dev_warn(dev, "failed to create radio device for %pOF\n", node);
			of_node_clear_flag(node, OF_POPULATED);
			return PTR_ERR(radio);
		}
		found++;
	}

	if (found < 2) {
		dev_err(dev, "found %u radio devices, expected 2\n", found);
		return -EINVAL;
	}

	return 0;
}

static int __sx130x_check_radio_device(struct device *dev, void *data)
{
	struct device *host = data;

	if (dev->bus == &sx130x_radio_bus_type && device_attach(dev) != 1) {
		dev_err(host, "radio %s not attached to driver\n", dev_name(dev));
		return -EBUSY;
	}

	return 0;
}

bool sx130x_radio_devices_okay(struct device *dev)
{
	int ret;

	ret = device_for_each_child(dev, dev, __sx130x_check_radio_device);
	if (ret)
		return false;

	return true;
}

static int __sx130x_unregister_radio_device(struct device *dev, void *data)
{
	sx130x_unregister_radio_device(to_sx130x_radio_device(dev));

	return 0;
}

void sx130x_unregister_radio_devices(struct device *dev)
{
	device_for_each_child(dev, NULL, __sx130x_unregister_radio_device);
}

static void devm_sx130x_unregister_radio_devices(struct device *dev, void *res)
{
	struct device **ptr = res;

	sx130x_unregister_radio_devices(*ptr);
}

int devm_sx130x_register_radio_devices(struct device *dev)
{
	struct device **ptr;
	int ret;

	ptr = devres_alloc(devm_sx130x_unregister_radio_devices, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = sx130x_register_radio_devices(dev);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	*ptr = dev;
	devres_add(dev, ptr);

	return 0;
}
