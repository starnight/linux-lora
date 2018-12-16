/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause */
/*-
 * LoRaWAN stack related definitions
 *
 * Copyright (c) 2018 Jian-Hong, Pan <starnight@g.ncu.edu.tw>
 *
 */

#ifndef __LORAWAN_NET_DEVICE_H__
#define __LORAWAN_NET_DEVICE_H__

enum {
	LRW_ADDR_APPEUI,
	LRW_ADDR_DEVEUI,
	LRW_ADDR_DEVADDR,
};

struct lrw_addr_in {
	int addr_type;
	union {
		u64 app_eui;
		u64 dev_eui;
		u32 devaddr;
	};
};

struct sockaddr_lorawan {
	sa_family_t family; /* AF_LORAWAN */
	struct lrw_addr_in addr_in;
};

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
