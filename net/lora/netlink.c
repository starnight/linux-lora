// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018 Andreas Färber
 */

#include <linux/module.h>
#include <net/genetlink.h>

// nllora.h
#define NLLORA_GENL_NAME "nllora"

enum nllora_attrs {
	NLLORA_ATTR_UNSPEC = 0,

	NLLORA_ATTR_FOO,

	__NLLORA_ATTR_AFTER_LAST,
	NLLORA_ATTR_MAX = __NLLORA_ATTR_AFTER_LAST - 1,
};

enum nllora_commands {
	NLLORA_CMD_UNSPEC = 0,

	NLLORA_CMD_FOO,

	__NLLORA_CMD_AFTER_LAST,
	NLLORA_CMD_MAX = __NLLORA_CMD_AFTER_LAST - 1,
};

// end nllora.h

enum nllora_multicast_groups {
	NLLORA_MCGRP_CONFIG = 0,
};

static const struct genl_multicast_group nllora_mcgrps[] = {
	[NLLORA_MCGRP_CONFIG] = { .name = "config" },
};

static int nllora_cmd_foo(struct sk_buff *skb, struct genl_info *info)
{
	return 0;
}

static const struct nla_policy nllora_policy[NLLORA_ATTR_MAX + 1] = {
	[NLLORA_ATTR_FOO] = { .type = NLA_U32 },
};

static const struct genl_ops nllora_ops[] = {
	{
		.cmd = NLLORA_CMD_FOO,
		.doit = nllora_cmd_foo,
		.policy = nllora_policy,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = 0,
	},
};

static struct genl_family nllora_fam __ro_after_init = {
	.name = NLLORA_GENL_NAME,
	.hdrsize = 0,
	.version = 1,
	.maxattr = NLLORA_ATTR_MAX,
	.netnsok = true,
	.module = THIS_MODULE,
	.ops = nllora_ops,
	.n_ops = ARRAY_SIZE(nllora_ops),
	.mcgrps = nllora_mcgrps,
	.n_mcgrps = ARRAY_SIZE(nllora_mcgrps),
};

static int __init nllora_init(void)
{
	int ret;

	ret = genl_register_family(&nllora_fam);
	if (ret)
		return ret;

	return 0;
}

static void __exit nllora_exit(void)
{
	genl_unregister_family(&nllora_fam);
}

subsys_initcall(nllora_init);
module_exit(nllora_exit);

MODULE_DESCRIPTION("LoRa netlink driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
