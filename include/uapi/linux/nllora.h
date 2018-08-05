/* SPDX-License-Identifier: (GPL-2.0-or-later WITH Linux-syscall-note) */
/*
 * LoRa Netlink interface
 *
 * Copyright (c) 2018 Andreas Färber
 */
#ifndef __LINUX_NLLORA_H
#define __LINUX_NLLORA_H

#define NLLORA_GENL_NAME "nllora"

enum nllora_attrs {
	NLLORA_ATTR_UNSPEC = 0,

	NLLORA_ATTR_IFINDEX,

	NLLORA_ATTR_FREQ,

	__NLLORA_ATTR_AFTER_LAST,
	NLLORA_ATTR_MAX = __NLLORA_ATTR_AFTER_LAST - 1,
};

enum nllora_commands {
	NLLORA_CMD_UNSPEC = 0,

	NLLORA_CMD_GET_FREQ,

	__NLLORA_CMD_AFTER_LAST,
	NLLORA_CMD_MAX = __NLLORA_CMD_AFTER_LAST - 1,
};

#endif
