// SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause
/*
 * LoRaWAN soft MAC
 *
 * Copyright (c) 2018 Jian-Hong, Pan <starnight@g.ncu.edu.tw>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>

#include <linux/lora/lorawan.h>
#include <linux/lora/lorawan_netdev.h>
#include "maclorawan.h"

#define	PHY_NAME		"lora"

/* Need to find a way to define or assign */
#define	LORAWAN_MTU		20

static struct class *lrw_sys_class;

static void
lrw_if_setup(struct net_device *ndev)
{
	ndev->addr_len = LRW_DEVADDR_LEN;
	memset(ndev->broadcast, 0xFF, ndev->addr_len);
	ndev->type = ARPHRD_LORAWAN;

	ndev->hard_header_len = LRW_MHDR_LEN + LRW_FHDR_MAX_LEN + LRW_FPORT_LEN;
	ndev->needed_tailroom = LRW_MIC_LEN;

	/**
	 * TODO: M should be a dynamic value defined by Regional Parameters,
	 *	 Being fixed for now.  Going to be changed.
	 */
	ndev->mtu = LORAWAN_MTU;
}

/**
 * lrw_alloc_hw - Allocate a memory space for the LoRa device
 * @priv_data_len:	the private data size
 * @lrw_operations:	the implemented operations of the LoRa device
 *
 * Return:		address of the LoRa device or NULL for failed
 */
struct lrw_hw *
lrw_alloc_hw(size_t priv_data_len, struct lrw_operations *ops)
{
	struct lrw_struct *lrw_st;
	struct net_device *ndev;
	int ret;

	if (WARN_ON(!ops || !ops->start || !ops->stop || !ops->xmit_async ||
		    !ops->set_txpower || !ops->set_dr ||
		    !ops->start_rx_window || !ops->set_state))
		return NULL;

	/* In memory it'll be like this:
	 *
	 * +-----------------------+
	 * | struct net_device     |
	 * +-----------------------+
	 * | struct lrw_struct     |
	 * +-----------------------+
	 * | driver's private data |
	 * +-----------------------+
	 */
	ndev = alloc_netdev(sizeof(struct lrw_struct) + priv_data_len,
			    PHY_NAME "%d", NET_NAME_ENUM, lrw_if_setup);
	if (!ndev)
		return ERR_PTR(-ENOMEM);
	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0)
		goto lrw_alloc_hw_err;

	lrw_st = (struct lrw_struct *)netdev_priv(ndev);
	lrw_st->ndev = ndev;

	lrw_st->state = LRW_STOP;
	lrw_st->ops = ops;
	lrw_st->hw.priv = (u8 *)lrw_st + sizeof(struct lrw_struct);
	skb_queue_head_init(&lrw_st->rx_skb_list);

	ndev->flags |= IFF_NOARP;
	ndev->features |= NETIF_F_HW_CSUM;

	return &lrw_st->hw;

lrw_alloc_hw_err:
	free_netdev(ndev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(lrw_alloc_hw);

/**
 * lrw_free_hw - Free the LoRa device's memory resource
 * @hw:		the LoRa device going to be freed
 */
void
lrw_free_hw(struct lrw_hw *hw)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	free_netdev(lrw_st->ndev);
}
EXPORT_SYMBOL(lrw_free_hw);

/**
 * lrw_set_deveui - Set the LoRa device's DevEUI
 * @hw:		the LoRa device going to be set
 * @eui:	the global end-device ID in IEEE EUI64 address space
 */
void
lrw_set_deveui(struct lrw_hw *hw, u64 eui)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	lrw_st->dev_eui = eui;
}
EXPORT_SYMBOL(lrw_set_deveui);

/**
 * lrw_get_deveui - Get the LoRa device's DevEUI
 * @hw:		the LoRa device going to be got from
 *
 * Return:	the device's DevEUI in IEEE EUI64 address space
 */
u64
lrw_get_deveui(struct lrw_hw *hw)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	return lrw_st->dev_eui;
}
EXPORT_SYMBOL(lrw_get_deveui);

/**
 * lrw_set_appeui - Set the LoRa device's AppEUI
 * @hw:		the LoRa device going to be set
 * @eui:	the global end-device ID in IEEE EUI64 address space
 */
void
lrw_set_appeui(struct lrw_hw *hw, u64 eui)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	lrw_st->app_eui = eui;
}
EXPORT_SYMBOL(lrw_set_appeui);

/**
 * lrw_get_appeui - Get the LoRa device's AppEUI
 * @hw:		the LoRa device going to be got from
 *
 * Return:	the device's AppEUI in IEEE EUI64 address space
 */
u64
lrw_get_appeui(struct lrw_hw *hw)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	return lrw_st->app_eui;
}
EXPORT_SYMBOL(lrw_get_appeui);

/**
 * lrw_set_devaddr - Set the LoRa device's address
 * @hw:		the LoRa device going to be set
 * @devaddr:	the device address
 */
void
lrw_set_devaddr(struct lrw_hw *hw, u32 devaddr)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	lrw_st->devaddr = devaddr;
}
EXPORT_SYMBOL(lrw_set_devaddr);

/**
 * lrw_get_devaddr - Get the LoRa device's address
 * @hw:		the LoRa device going to be got from
 *
 * Return:	the device address
 */
u32
lrw_get_devaddr(struct lrw_hw *hw)
{
	struct lrw_struct *lrw_st;

	lrw_st = container_of(hw, struct lrw_struct, hw);
	return lrw_st->devaddr;
}
EXPORT_SYMBOL(lrw_get_devaddr);

/**
 * lrw_add_hw - Add a LoRaWAN hardware as a network device
 * @lrw_st:	the LoRa device going to be added
 *
 * Return:	0 / other number for success / failed
 */
static int
lrw_add_hw(struct lrw_struct *lrw_st)
{
	struct net_device *ndev = lrw_st->ndev;
	__be32 be_addr;
	int ret;

	lrw_st->fcnt_up = 0;
	lrw_st->fcnt_down = 0;
	lrw_st->_cur_ss = NULL;

	mutex_init(&lrw_st->ss_list_lock);
	INIT_LIST_HEAD(&lrw_st->ss_list);

	tasklet_init(&lrw_st->xmit_task, lrw_xmit, (unsigned long)lrw_st);
	INIT_WORK(&lrw_st->rx_work, lrw_rx_work);

	be_addr = cpu_to_be32(lrw_st->devaddr);
	memcpy(ndev->perm_addr, &be_addr, ndev->addr_len);
	memcpy(ndev->dev_addr, ndev->perm_addr, ndev->addr_len);

	write_pnet(&lrw_st->_net, &init_net);
	ret = register_netdev(ndev);

	return ret;
}

/**
 * lrw_remove_hw - Remove a LoRaWAN hardware from a network device
 * @lrw_st:	the LoRa device going to be removed
 */
static void
lrw_remove_hw(struct lrw_struct *lrw_st)
{
	unregister_netdev(lrw_st->ndev);
	tasklet_kill(&lrw_st->xmit_task);
}

bool
ready2write(struct lrw_struct *lrw_st)
{
	bool status = false;

	if (!lrw_st->_cur_ss && lrw_st->state == LRW_STATE_IDLE)
		status = true;

	return status;
}

bool
ready2read(struct lrw_struct *lrw_st)
{
	struct lrw_session *ss;
	bool status = false;

	if (!list_empty(&lrw_st->ss_list) && lrw_st->state != LRW_STOP) {
		ss = list_first_entry(&lrw_st->ss_list,
				      struct lrw_session,
				      entry);
		if (ss->state == LRW_RXRECEIVED_SS)
			status = true;
	}

	return status;
}

static int
lrw_if_up(struct net_device *ndev)
{
	struct lrw_struct *lrw_st = NETDEV_2_LRW(ndev);
	int ret = -EBUSY;

	if (lrw_st->state == LRW_STOP) {
		ret = lrw_start_hw(lrw_st);
		netif_start_queue(ndev);
	}

	return ret;
}

static int
lrw_if_down(struct net_device *ndev)
{
	struct lrw_struct *lrw_st = NETDEV_2_LRW(ndev);

	if (lrw_st->state != LRW_STOP) {
		netif_stop_queue(ndev);
		lrw_stop_hw(lrw_st);
	}

	return 0;
}

static netdev_tx_t
lrw_if_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct lrw_struct *lrw_st = NETDEV_2_LRW(ndev);
	struct lrw_session *ss;
	netdev_tx_t ret;

	ret = NETDEV_TX_OK;

	ss = lrw_alloc_ss(lrw_st);
	if (!ss)
		return NETDEV_TX_BUSY;

	mutex_lock(&lrw_st->ss_list_lock);
	if (ready2write(lrw_st)) {
		list_add_tail(&ss->entry, &lrw_st->ss_list);
		lrw_st->state = LRW_STATE_TX;
		lrw_st->_cur_ss = ss;
		ss->fcnt_up = lrw_st->fcnt_up;
		ss->fcnt_down = lrw_st->fcnt_down;
		/* TODO: RX delay #1/#2 should be set by regional parameters */
		ss->rx_delay1 = 1;
		ss->rx_delay2 = 2;
		ss->rx1_window = 500;
		ss->rx2_window = 500;
	} else {
		ret = NETDEV_TX_BUSY;
	}
	mutex_unlock(&lrw_st->ss_list_lock);

	if (ret == NETDEV_TX_OK) {
		ss->state = LRW_INIT_SS;
		ss->tx_skb = skb;
		lrw_prepare_tx_frame(ss);
		tasklet_schedule(&lrw_st->xmit_task);
	} else {
		lrw_free_ss(ss);
	}

	return ret;
}

static int
lrw_if_get_addr(struct lrw_struct *lrw_st, struct sockaddr_lorawan *addr)
{
	int ret = 0;

	switch (addr->addr_in.addr_type) {
	case LRW_ADDR_DEVADDR:
		addr->addr_in.devaddr = lrw_st->devaddr;
		break;
	case LRW_ADDR_DEVEUI:
		addr->addr_in.dev_eui = lrw_st->dev_eui;
		break;
	case LRW_ADDR_APPEUI:
		addr->addr_in.app_eui = lrw_st->app_eui;
		break;
	default:
		ret = -ENOTSUPP;
	}

	return ret;
}

static int
lrw_if_set_addr(struct lrw_struct *lrw_st, struct sockaddr_lorawan *addr)
{
	struct lrw_hw *hw = &lrw_st->hw;
	int ret = 0;

	if (netif_running(lrw_st->ndev))
		return -EBUSY;

	switch (addr->addr_in.addr_type) {
	case LRW_ADDR_DEVADDR:
		lrw_set_devaddr(hw, addr->addr_in.devaddr);
		break;
	case LRW_ADDR_DEVEUI:
		lrw_set_deveui(hw, addr->addr_in.dev_eui);
		break;
	case LRW_ADDR_APPEUI:
		lrw_set_appeui(hw, addr->addr_in.app_eui);
		break;
	default:
		ret = -ENOTSUPP;
	}

	return ret;
}

static void
swap_bytes(u8 *dst, u8 *src, size_t l)
{
	/* Human reading is big-endian, but LoRaWAN is little-endian */
	unsigned int i;

	for (i = 0; i < l; i++)
		dst[i] = src[l - i - 1];
}

int
lrw_set_key(struct lrw_hw *hw, u8 type, u8 *key, size_t key_len)
{
	struct lrw_struct *lrw_st;
	int ret = 0;

	lrw_st = container_of(hw, struct lrw_struct, hw);

	netdev_dbg(lrw_st->ndev, "%s: type=%d\n", __func__, type);
	if (lrw_st->state != LRW_STOP)
		return -EINVAL;

	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET,
		       16, 1, key, key_len, true);
	switch (type) {
	case LRW_APPKEY:
		swap_bytes(lrw_st->appkey, key, key_len);
		break;
	case LRW_NWKSKEY:
		swap_bytes(lrw_st->nwkskey, key, key_len);
		break;
	case LRW_APPSKEY:
		swap_bytes(lrw_st->appskey, key, key_len);
		break;
	default:
		ret = -ENOTSUPP;
	}

	return ret;
}
EXPORT_SYMBOL(lrw_set_key);

int
lrw_get_key(struct lrw_hw *hw, u8 type, u8 *key, size_t key_len)
{
	struct lrw_struct *lrw_st;
	int ret = 0;

	lrw_st = container_of(hw, struct lrw_struct, hw);

	netdev_dbg(lrw_st->ndev, "%s: type=%d\n", __func__, type);
	switch (type) {
	case LRW_APPKEY:
		swap_bytes(key, lrw_st->appkey, key_len);
		break;
	case LRW_NWKSKEY:
		swap_bytes(key, lrw_st->nwkskey, key_len);
		break;
	case LRW_APPSKEY:
		swap_bytes(key, lrw_st->appskey, key_len);
		break;
	default:
		ret = -ENOTSUPP;
	}

	return ret;
}

static int
lrw_if_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	struct lrw_struct *lrw_st = NETDEV_2_LRW(ndev);
	struct sockaddr_lorawan *addr;
	int ret = 0;

	netdev_dbg(ndev, "%s: ioctl file (cmd=0x%X)\n", __func__, cmd);

	/* I/O control by each command */
	switch (cmd) {
	/* Set & get the DevAddr, DevEUI and AppEUI */
	case SIOCSIFADDR:
		addr = (struct sockaddr_lorawan *)&ifr->ifr_addr;
		ret = lrw_if_set_addr(lrw_st, addr);
		break;
	case SIOCGIFADDR:
		addr = (struct sockaddr_lorawan *)&ifr->ifr_addr;
		ret = lrw_if_get_addr(lrw_st, addr);
		break;
	default:
		ret = -ENOTSUPP;
	}

	return ret;
}

static int
lrw_if_set_mac(struct net_device *ndev, void *p)
{
	struct lrw_struct *lrw_st = NETDEV_2_LRW(ndev);
	struct sockaddr *addr = p;
	__be32 *be_addr;

	be_addr = (__be32 *)addr->sa_data;

	netdev_dbg(ndev, "%s: AF_TYPE:%d set mac address %X\n",
		   __func__, addr->sa_family, be32_to_cpu(*be_addr));

	if (netif_running(ndev))
		return -EBUSY;

	lrw_set_devaddr(&lrw_st->hw, be32_to_cpu(*be_addr));
	memcpy(ndev->dev_addr, be_addr, ndev->addr_len);

	return 0;
}

static const struct net_device_ops lrw_if_ops = {
	.ndo_open = lrw_if_up,
	.ndo_stop = lrw_if_down,
	.ndo_start_xmit = lrw_if_start_xmit,
	.ndo_do_ioctl = lrw_if_ioctl,
	.ndo_set_mac_address = lrw_if_set_mac,
};

/**
 * lrw_register_hw - Register as a LoRaWAN compatible device
 * @hw:		LoRa device going to be registered
 *
 * Return:	0 / negative number for success / error number
 */
int
lrw_register_hw(struct lrw_hw *hw)
{
	struct lrw_struct *lrw_st = container_of(hw, struct lrw_struct, hw);
	int ret;

	device_initialize(&lrw_st->dev);
	dev_set_name(&lrw_st->dev, netdev_name(lrw_st->ndev));
	lrw_st->dev.class = lrw_sys_class;
	lrw_st->dev.platform_data = lrw_st;

	ret = device_add(&lrw_st->dev);
	if (ret)
		goto lrw_register_hw_end;

	/* Add a LoRa device node as a network device */
	lrw_st->ndev->netdev_ops = &lrw_if_ops;
	ret = lrw_add_hw(lrw_st);
	if (!ret)
		netdev_info(lrw_st->ndev, "register\n");

lrw_register_hw_end:
	return ret;
}
EXPORT_SYMBOL(lrw_register_hw);

/**
 * lrw_unregister_hw - Unregister the LoRaWAN compatible device
 * @hw:		LoRa device going to be unregistered
 */
void
lrw_unregister_hw(struct lrw_hw *hw)
{
	struct lrw_struct *lrw_st = container_of(hw, struct lrw_struct, hw);

	netdev_info(lrw_st->ndev, "unregister\n");

	/* Stop and remove the LoRaWAM hardware from system */
	if (lrw_st->state != LRW_STOP)
		lrw_stop_hw(lrw_st);
	device_del(&lrw_st->dev);
	lrw_remove_hw(lrw_st);
}
EXPORT_SYMBOL(lrw_unregister_hw);

static int __init
lrw_init(void)
{
	int err = 0;

	pr_info("%s: module inserted\n", LORAWAN_MODULE_NAME);

	/* Create device class */
	lrw_sys_class = class_create(THIS_MODULE, LORAWAN_MODULE_NAME);
	if (IS_ERR(lrw_sys_class)) {
		pr_err("%s: Failed to create a class of LoRaWAN\n",
		       LORAWAN_MODULE_NAME);
		err = PTR_ERR(lrw_sys_class);
		goto lrw_init_end;
	}

	pr_debug("%s: class created\n", LORAWAN_MODULE_NAME);

lrw_init_end:
	return err;
}

static void __exit
lrw_exit(void)
{
	/* Delete device class */
	class_destroy(lrw_sys_class);
	pr_info("%s: module removed\n", LORAWAN_MODULE_NAME);
}

module_init(lrw_init);
module_exit(lrw_exit);

MODULE_AUTHOR("Jian-Hong Pan, <starnight@g.ncu.edu.tw>");
MODULE_DESCRIPTION("LoRaWAN soft MAC stack");
MODULE_LICENSE("Dual BSD/GPL");
