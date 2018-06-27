/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Semtech SX1301 LoRa concentrator
 *
 * Copyright (c) 2018 Ben Whitten
 * Copyright (c) 2018 Andreas Färber
 */
#ifndef LORA_SX130X_H
#define LORA_SX130X_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>

struct sx130x_radio_device {
	struct device dev;
	struct device *concentrator;
	const struct regmap_bus *regmap_bus;
	struct regmap *regmap;
	unsigned int nr;
};

static inline struct sx130x_radio_device *to_sx130x_radio_device(struct device *dev)
{
	return dev ? container_of(dev, struct sx130x_radio_device, dev) : NULL;
}

static inline void sx130x_radio_put(struct sx130x_radio_device *radio)
{
	if (radio)
		put_device(&radio->dev);
}

struct sx130x_radio_driver {
	int (*probe)(struct sx130x_radio_device *rdev);
	int (*remove)(struct sx130x_radio_device *rdev);
	struct device_driver driver;
};

static inline struct sx130x_radio_driver *to_sx130x_radio_driver(struct device_driver *drv)
{
	return drv ? container_of(drv, struct sx130x_radio_driver, driver) : NULL;
}

extern int __sx130x_register_radio_driver(struct module *owner, struct sx130x_radio_driver *driver);

static inline void sx130x_unregister_radio_driver(struct sx130x_radio_driver *driver)
{
	if (driver)
		driver_unregister(&driver->driver);
}

#define sx130x_register_radio_driver(driver) \
	__sx130x_register_radio_driver(THIS_MODULE, driver)

#endif
