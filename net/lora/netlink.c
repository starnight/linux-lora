// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018 Andreas Färber
 */

#include <linux/if_arp.h>
#include <linux/lora.h>
#include <linux/module.h>
#include <linux/nllora.h>
#include <linux/lora/dev.h>
#include <net/genetlink.h>
#include <net/sock.h>

enum nllora_multicast_groups {
	NLLORA_MCGRP_CONFIG = 0,
};

static const struct genl_multicast_group nllora_mcgrps[] = {
	[NLLORA_MCGRP_CONFIG] = { .name = "config" },
};

static struct genl_family nllora_fam;

static int nllora_cmd_get_freq(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = genl_family_attrbuf(&nllora_fam);
	bool have_ifindex = attrs[NLLORA_ATTR_IFINDEX];
	struct sk_buff *msg;
	struct net_device *netdev;
	struct lora_dev_priv *priv;
	void *hdr;
	int ifindex = -1;

	if (have_ifindex)
		ifindex = nla_get_u32(attrs[NLLORA_ATTR_IFINDEX]);

	netdev = dev_get_by_index(sock_net(skb->sk), ifindex);
	if (!netdev)
		return -ENOBUFS;

	priv = netdev_priv(netdev);
	if (netdev->type != ARPHRD_LORA || priv->magic != LORA_DEV_MAGIC) {
		dev_put(netdev);
		return -ENOBUFS;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		dev_put(netdev);
		return -ENOMEM;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &nllora_fam, 0, NLLORA_CMD_GET_FREQ);
	nla_put_u32(msg, NLLORA_ATTR_IFINDEX, ifindex);

	if (!priv->get_freq) {
		dev_put(netdev);
		genlmsg_cancel(msg, hdr);
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	nla_put_u32(msg, NLLORA_ATTR_FREQ, priv->get_freq(netdev));

	dev_put(netdev);

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);
}

static const struct nla_policy nllora_policy[NLLORA_ATTR_MAX + 1] = {
	[NLLORA_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NLLORA_ATTR_FREQ] = { .type = NLA_U32 },
};

static const struct genl_ops nllora_ops[] = {
	{
		.cmd = NLLORA_CMD_GET_FREQ,
		.doit = nllora_cmd_get_freq,
		.policy = nllora_policy,
		.flags = 0/*GENL_ADMIN_PERM*/,
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
