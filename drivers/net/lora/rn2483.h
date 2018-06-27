/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef _RN2483_H
#define _RN2483_H

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/netdevice.h>
#include <linux/serdev.h>
#include <linux/lora/dev.h>

struct rn2483_device {
	struct serdev_device *serdev;
	struct gpio_desc *reset_gpio;
	struct net_device *netdev;
	unsigned model;
	lora_eui hweui;
	unsigned band;
	bool saw_cr;
	void *buf;
	size_t buflen;
	struct completion line_recv_comp;
	struct completion line_read_comp;
	struct mutex cmd_lock;
};

int rn2483_readline_timeout(struct rn2483_device *rndev, char **line, unsigned long timeout);
int rn2483_send_command_timeout(struct rn2483_device *rndev,
	const char *cmd, char **resp, unsigned long timeout);

int rn2483_sys_get_hweui(struct rn2483_device *rndev, lora_eui *val);
int rn2483_mac_get_band(struct rn2483_device *rndev, uint *val);
int rn2483_mac_get_status(struct rn2483_device *rndev, u32 *val);
int rn2483_mac_reset_band(struct rn2483_device *rndev, unsigned band);
int rn2483_mac_pause(struct rn2483_device *rndev, u32 *max_pause);
int rn2483_mac_resume(struct rn2483_device *rndev);

#endif
