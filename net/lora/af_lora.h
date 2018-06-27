/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef AF_LORA_H
#define AF_LORA_H

extern struct proto dgram_proto;
extern const struct proto_ops dgram_proto_ops;

int lora_send(struct sk_buff *skb);

#endif /* AF_LORA_H */
