/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/lora/dev.h
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef _LORA_DEV_H
#define _LORA_DEV_H

#include <linux/netdevice.h>

struct net_device *alloc_loradev(int sizeof_priv);
void free_loradev(struct net_device *dev);
int register_loradev(struct net_device *dev);
void unregister_loradev(struct net_device *dev);
int open_loradev(struct net_device *dev);
void close_loradev(struct net_device *dev);

struct lora_dev_priv {
	struct net_device *dev;
};

#endif /* _LORA_DEV_H */
