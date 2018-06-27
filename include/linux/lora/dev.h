/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/lora/dev.h
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef _LORA_DEV_H
#define _LORA_DEV_H

#include <linux/netdevice.h>

typedef u8 lora_eui[8];

#define PRIxLORAEUI "%02x%02x%02x%02x%02x%02x%02x%02x"
#define PRIXLORAEUI "%02X%02X%02X%02X%02X%02X%02X%02X"
#define LORA_EUI(x) x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]

static inline int lora_strtoeui(const char *str, lora_eui *val)
{
        char buf[3];
        int i, ret;

        for (i = 0; i < 8; i++) {
                strncpy(buf, str + i * 2, 2);
                buf[2] = 0;
                ret = kstrtou8(buf, 16, &(*val)[i]);
                if (ret)
                        return ret;
        }
        return 0;
}

struct net_device *alloc_loradev(int sizeof_priv);
struct net_device *devm_alloc_loradev(struct device *dev, size_t priv);
void free_loradev(struct net_device *dev);
int register_loradev(struct net_device *dev);
void unregister_loradev(struct net_device *dev);
int open_loradev(struct net_device *dev);
void close_loradev(struct net_device *dev);

#define LORA_DEV_MAGIC 0x4c6f5261 /* LoRa */

struct lora_dev_priv {
	u32 magic;
	struct net_device *dev;

	u32 (*get_freq)(struct net_device *netdev);
};

#endif /* _LORA_DEV_H */
