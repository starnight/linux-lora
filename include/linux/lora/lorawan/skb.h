/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause */
/*
 * LoRaWAN socket buffer related definitions
 *
 * Copyright (c) 2018 Jian-Hong Pan <starnight@g.ncu.edu.tw>
 */

#ifndef __LORAWAN_SKB_H__
#define __LORAWAN_SKB_H__

#include <linux/skbuff.h>

/**
 * lrw_mac_cb - This structure holds the control buffer (cb) of sk_buff
 *
 * @devaddr:	the LoRaWAN device address of this LoRaWAN hardware
 */
struct lrw_mac_cb {
	u32 devaddr;
};

/**
 * lrw_get_mac_cb - Get the LoRaWAN MAC control buffer of the sk_buff
 * @skb:	the exchanging sk_buff
 *
 * Return:	the pointer of LoRaWAN MAC control buffer
 */
static inline struct lrw_mac_cb *lrw_get_mac_cb(struct sk_buff *skb)
{
	return (struct lrw_mac_cb *)skb->cb;
}

#endif
