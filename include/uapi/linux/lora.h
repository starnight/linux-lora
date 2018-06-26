/* SPDX-License-Identifier: (GPL-2.0-or-later WITH Linux-syscall-note) */
/*
 * linux/lora.h
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */
#ifndef _UAPI_LINUX_LORA_H
#define _UAPI_LINUX_LORA_H

#include <linux/types.h>
#include <linux/socket.h>

/* particular protocols of the protocol family PF_LORA */
#define LORA_PROTO_DATAGRAM	0
#define LORA_NPROTO		1

struct sockaddr_lora {
	__kernel_sa_family_t lora_family;
	int lora_ifindex;
	union {
	} lora_addr;
};

#endif /* _UAPI_LINUX_LORA_H */
