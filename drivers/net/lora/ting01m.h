/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Widora   Ting-01M
 * Himalaya HIMO-01M
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef LORA_TING01M_H
#define LORA_TING01M_H

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/netdevice.h>
#include <linux/serdev.h>

struct widora_device {
	struct serdev_device *serdev;
	struct net_device *netdev;

	struct gpio_desc *rst;
#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio;
#endif

	char rx_buf[4096];
	int rx_len;

	struct completion line_recv_comp;
};

int widora_send_command(struct widora_device *widev, const char *cmd, char **data, unsigned long timeout);
int widora_simple_cmd(struct widora_device *widev, const char *cmd, unsigned long timeout);

int widora_do_reset(struct widora_device *widev, unsigned long timeout);
int widora_get_version(struct widora_device *widev, char **version, unsigned long timeout);

int widora_set_gpio(struct widora_device *widev, char bank, char pin, bool enabled, unsigned long timeout);

#endif /* LORA_TING01M_H */
