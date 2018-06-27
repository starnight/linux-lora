/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/lora/skb.h
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef _LORA_SKB_H
#define _LORA_SKB_H

#include <linux/types.h>
#include <linux/skbuff.h>

struct lora_skb_priv {
	int ifindex;
};

static inline struct lora_skb_priv *lora_skb_prv(struct sk_buff *skb)
{
	return (struct lora_skb_priv *)(skb->head);
}

static inline void lora_skb_reserve(struct sk_buff *skb)
{
	skb_reserve(skb, sizeof(struct lora_skb_priv));
}

struct sk_buff *alloc_lora_skb(struct net_device *dev, u8 **data);

#endif /* _LORA_SKB_H */
