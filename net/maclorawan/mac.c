// SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause
/*
 * LoRaWAN soft MAC
 *
 * Copyright (c) 2018 Jian-Hong, Pan <starnight@g.ncu.edu.tw>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/lora/lorawan.h>
#include <linux/lora/lorawan/skb.h>
#include <linux/lora/lorawan_netdev.h>

#include "maclorawan.h"
#include "crypto.h"

static void rx_timeout_work(struct work_struct *work);

struct lrw_session *
lrw_alloc_ss(struct lrw_struct *lrw_st)
{
	struct lrw_session *ss;

	ss = kzalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss)
		goto lrw_alloc_ss_end;

	ss->lrw_st = lrw_st;
	ss->devaddr = lrw_st->devaddr;
	INIT_LIST_HEAD(&ss->entry);

	ss->tx_should_ack = false;
	ss->retry = 3;
	spin_lock_init(&ss->state_lock);
	INIT_WORK(&ss->timeout_work, rx_timeout_work);

lrw_alloc_ss_end:
	return ss;
}

void
lrw_free_ss(struct lrw_session *ss)
{
	netdev_dbg(ss->lrw_st->ndev, "%s\n", __func__);
	if (ss->tx_skb)
		consume_skb(ss->tx_skb);
	netdev_dbg(ss->lrw_st->ndev, "%s: free rx skb\n", __func__);
	if (ss->rx_skb)
		consume_skb(ss->rx_skb);

	netdev_dbg(ss->lrw_st->ndev, "%s: free ss\n", __func__);
	kfree(ss);
}

void
lrw_del_ss(struct lrw_session *ss)
{
	netdev_dbg(ss->lrw_st->ndev, "%s\n", __func__);
	list_del(&ss->entry);
	lrw_free_ss(ss);
}

void
lrw_del_all_ss(struct lrw_struct *lrw_st)
{
	struct lrw_session *ss, *tmp;

	mutex_lock(&lrw_st->ss_list_lock);
	lrw_st->_cur_ss = NULL;
	list_for_each_entry_safe(ss, tmp, &lrw_st->ss_list, entry) {
		del_timer(&ss->timer);
		lrw_del_ss(ss);
	}
	mutex_unlock(&lrw_st->ss_list_lock);
}

void
lrw_ready_hw(struct lrw_struct *lrw_st)
{
	lrw_st->state = LRW_STATE_IDLE;
}

int
lrw_start_hw(struct lrw_struct *lrw_st)
{
	int ret = 0;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);
	lrw_st->nwks_shash_tfm = lrw_mic_key_setup(lrw_st->nwkskey,
						   LRW_KEY_LEN);
	lrw_st->nwks_skc_tfm = lrw_encrypt_key_setup(lrw_st->nwkskey,
						     LRW_KEY_LEN);
	lrw_st->apps_skc_tfm = lrw_encrypt_key_setup(lrw_st->appskey,
						     LRW_KEY_LEN);
	lrw_st->state = LRW_START;
	ret = lrw_st->ops->start(&lrw_st->hw);
	if (!ret)
		lrw_ready_hw(lrw_st);

	return ret;
}

void
lrw_stop_hw(struct lrw_struct *lrw_st)
{
	netdev_dbg(lrw_st->ndev, "%s\n", __func__);
	lrw_st->state = LRW_STOP;
	netdev_dbg(lrw_st->ndev, "%s: going to stop hardware\n", __func__);
	lrw_st->ops->stop(&lrw_st->hw);

	netdev_dbg(lrw_st->ndev, "%s: going to kill tasks & flush works",
		   __func__);
	tasklet_kill(&lrw_st->xmit_task);
	flush_work(&lrw_st->rx_work);

	netdev_dbg(lrw_st->ndev, "%s: going to delete all session\n", __func__);
	lrw_del_all_ss(lrw_st);

	netdev_dbg(lrw_st->ndev, "%s: going to free mic tfm\n", __func__);
	lrw_mic_key_free(lrw_st->nwks_shash_tfm);
	netdev_dbg(lrw_st->ndev, "%s: going to free nwks tfm\n", __func__);
	lrw_encrypt_key_free(lrw_st->nwks_skc_tfm);
	netdev_dbg(lrw_st->ndev, "%s: going to free apps tfm\n", __func__);
	lrw_encrypt_key_free(lrw_st->apps_skc_tfm);
}

void
lrw_prepare_tx_frame(struct lrw_session *ss)
{
	struct lrw_struct *lrw_st = ss->lrw_st;
	struct sk_buff *skb = ss->tx_skb;
	u8 mhdr, fctrl, fport;
	u8 mic[LRW_MIC_LEN];
	__le32 le_devaddr;
	__le16 le_fcnt_up;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	/* Encrypt the plain buffer content */
	lrw_encrypt_buf(lrw_st->apps_skc_tfm, LRW_UPLINK,
			ss->devaddr, ss->fcnt_up, skb->data, skb->len);

	/* Push FPort */
	if (skb->len) {
		fport = ss->fport;
		memcpy(skb_push(skb, LRW_FPORT_LEN), &fport, LRW_FPORT_LEN);
	}

	/* Push FCnt_Up */
	le_fcnt_up = cpu_to_le16(ss->fcnt_up);
	memcpy(skb_push(skb, LRW_FCNT_LEN), &le_fcnt_up, LRW_FCNT_LEN);

	/* Push FCtrl */
	fctrl = 0;
	if (lrw_st->rx_should_ack) {
		fctrl |= 0x20;
		lrw_st->rx_should_ack = false;
	}
	memcpy(skb_push(skb, LRW_FCTRL_LEN), &fctrl, LRW_FCTRL_LEN);

	/* Push DevAddr */
	le_devaddr = cpu_to_le32(ss->devaddr);
	memcpy(skb_push(skb, LRW_DEVADDR_LEN), &le_devaddr, LRW_DEVADDR_LEN);

	/* Push MHDR */
	mhdr = LRW_UNCONFIRMED_DATA_UP << 5;
	if ((mhdr & (0x6 << 5)) == (0x4 << 5))
		ss->tx_should_ack = true;
	memcpy(skb_push(skb, LRW_MHDR_LEN), &mhdr, LRW_MHDR_LEN);

	/* Put MIC */
	lrw_calc_mic(lrw_st->nwks_shash_tfm, LRW_UPLINK,
		     ss->devaddr, ss->fcnt_up, skb->data, skb->len, mic);
	memcpy(skb_put(skb, LRW_MIC_LEN), mic, LRW_MIC_LEN);
}

void
lrw_xmit(unsigned long data)
{
	struct lrw_struct *lrw_st = (struct lrw_struct *)data;
	struct lrw_session *ss = lrw_st->_cur_ss;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);
	ss->state = LRW_XMITTING_SS;
	lrw_st->ops->xmit_async(&lrw_st->hw, ss->tx_skb);
}

int
lrw_parse_frame(struct lrw_session *ss, struct sk_buff *skb)
{
	struct lrw_struct *lrw_st = ss->lrw_st;
	struct lrw_fhdr *fhdr = &ss->rx_fhdr;
	__le16 *p_fcnt;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	/* Get message type */
	fhdr->mtype = skb->data[0];
	skb_pull(skb, LRW_MHDR_LEN);

	/* Trim Device Address */
	skb_pull(skb, LRW_DEVADDR_LEN);

	/* Get frame control */
	fhdr->fctrl = skb->data[0];
	skb_pull(skb, LRW_FCTRL_LEN);

	/* Ack the original TX frame if it should be acked */
	if (ss->tx_should_ack && (fhdr->fctrl & 0x20))
		ss->tx_should_ack = false;

	/* Get frame count */
	p_fcnt = (__le16 *)skb->data;
	fhdr->fcnt = le16_to_cpu(*p_fcnt);
	skb_pull(skb, LRW_FCNT_LEN);

	/* Get frame options */
	fhdr->fopts_len = fhdr->fctrl & 0xF;
	if (fhdr->fopts_len > 0) {
		if (skb->len < fhdr->fopts_len)
			goto lrw_parse_frame_err;
		memcpy(fhdr->fopts, skb->data, fhdr->fopts_len);
		skb_pull(skb, fhdr->fopts_len);
	}

	/* TODO: Parse frame options */

	/* Parse FPort and decrypt payload */
	if (skb->len > 0) {
		if (skb->len <= LRW_FPORT_LEN)
			goto lrw_parse_frame_err;

		/* TODO: Deal FPort */

		skb_pull(skb, LRW_FPORT_LEN);

		lrw_decrypt_buf(lrw_st->apps_skc_tfm, LRW_DOWNLINK,
				ss->devaddr, fhdr->fcnt, skb->data, skb->len);
	}

	return 0;

lrw_parse_frame_err:
	return -EMSGSIZE;
}

struct lrw_session *
lrw_rx_skb_2_session(struct lrw_struct *lrw_st, struct sk_buff *rx_skb)
{
	struct lrw_session *ss;
	__le16 *p_fcnt;
	u16 fcnt;
	u16 ofs;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	ofs = LRW_MHDR_LEN + LRW_DEVADDR_LEN + LRW_FCTRL_LEN;
	p_fcnt = (__le16 *)(rx_skb->data + ofs);
	fcnt = le16_to_cpu(*p_fcnt);

	/* Find the corresponding session */
	ss = lrw_st->_cur_ss;

	/* Frame count downlink check */
	if (fcnt >= (ss->fcnt_down & 0xFFFF))
		ss->rx_skb = rx_skb;
	else
		ss = NULL;

	return ss;
}

void
lrw_rx_work(struct work_struct *work)
{
	struct lrw_struct *lrw_st;
	struct lrw_session *ss;
	struct sk_buff *skb;

	lrw_st = container_of(work, struct lrw_struct, rx_work);

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	skb = skb_dequeue(&lrw_st->rx_skb_list);

	/* Check and parse the RX frame */
	ss = lrw_rx_skb_2_session(lrw_st, skb);
	if (!ss)
		goto lrw_rx_work_not_new_frame;

	if (lrw_parse_frame(ss, skb)) {
		ss->rx_skb = NULL;
		goto lrw_rx_work_not_new_frame;
	}

	/* Check the TX frame is acked or not */
	if (ss->tx_should_ack) {
		ss->rx_skb = NULL;
		goto lrw_rx_work_not_new_frame;
	}

	/* The TX frame is acked or no need to be acked */
	del_timer(&ss->timer);
	lrw_st->rx_should_ack = (ss->rx_fhdr.mtype & 0xC0) == 0x40;

	lrw_st->ndev->stats.rx_packets++;
	lrw_st->ndev->stats.rx_bytes += ss->rx_skb->len;

	spin_lock_bh(&ss->state_lock);
	ss->state = LRW_RXRECEIVED_SS;
	spin_unlock_bh(&ss->state_lock);

	if (ss->rx_skb->len > 0) {
		lrw_get_mac_cb(skb)->devaddr = lrw_st->devaddr;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->protocol = htons(ETH_P_LORAWAN);
		skb->pkt_type = PACKET_HOST;
		netif_receive_skb(skb);
	}

	ss->rx_skb = NULL;

	mutex_lock(&lrw_st->ss_list_lock);
	lrw_st->fcnt_down = ss->rx_fhdr.fcnt;
	lrw_st->_cur_ss = NULL;
	lrw_del_ss(ss);
	lrw_st->state = LRW_STATE_IDLE;
	mutex_unlock(&lrw_st->ss_list_lock);

	return;

lrw_rx_work_not_new_frame:
	/* Drop the RX frame if checked failed */
	if (skb)
		kfree_skb(skb);
}

int
lrw_check_mic(struct crypto_shash *tfm, struct sk_buff *skb)
{
	u8 cks[LRW_MIC_LEN];
	u32 devaddr;
	size_t len;
	u16 fcnt;
	u8 *buf;
	u8 *mic;
	u16 ofs;

	buf = skb->data;
	devaddr = le32_to_cpu(*((__le32 *)(buf + LRW_MHDR_LEN)));
	ofs = LRW_MHDR_LEN + LRW_DEVADDR_LEN + LRW_FCTRL_LEN;
	fcnt = le16_to_cpu(*(__le16 *)(buf + ofs));
	len = skb->len - LRW_MIC_LEN;
	mic = skb->data + len;

	lrw_calc_mic(tfm, LRW_DOWNLINK, devaddr, fcnt, buf, len, cks);

	return (!memcmp(cks, mic, LRW_MIC_LEN));
}

/**
 * lrw_rx_irqsave - Tell LoRaWAN module that there is new received frame
 * @hw:		the LoRa device
 * @skb:	the new received frame
 */
void
lrw_rx_irqsave(struct lrw_hw *hw, struct sk_buff *skb)
{
	struct lrw_struct *lrw_st = container_of(hw, struct lrw_struct, hw);
	u32 devaddr;
	u8 mtype;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	if (skb->len < LRW_MHDR_LEN + LRW_DEVADDR_LEN + LRW_FCTRL_LEN +
	    LRW_FCNT_LEN + LRW_MIC_LEN)
		goto lrw_rx_irqsave_err;

	mtype = skb->data[0] >> 5;
	devaddr = le32_to_cpu(*(__le32 *)(skb->data + LRW_MHDR_LEN));

	/* Check the frame is the downlink frame */
	if ((mtype == LRW_UNCONFIRMED_DATA_DOWN ||
	     mtype == LRW_CONFIRMED_DATA_DOWN) &&
	     devaddr == lrw_st->devaddr &&
	     lrw_check_mic(lrw_st->nwks_shash_tfm, skb)) {
		/* Remove message integrity code (MIC) */
		skb_trim(skb, skb->len - LRW_MIC_LEN);
		skb_queue_tail(&lrw_st->rx_skb_list, skb);
		schedule_work(&lrw_st->rx_work);
		return;
	}

lrw_rx_irqsave_err:
	kfree_skb(skb);
}
EXPORT_SYMBOL(lrw_rx_irqsave);

static void
lrw_rexmit(struct timer_list *timer)
{
	struct lrw_session *ss = container_of(timer, struct lrw_session, timer);
	struct lrw_struct *lrw_st = ss->lrw_st;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	lrw_st->state = LRW_STATE_TX;
	lrw_xmit((unsigned long)lrw_st);
}

static void
rx_timeout_work(struct work_struct *work)
{
	struct lrw_struct *lrw_st;
	struct lrw_session *ss;

	ss = container_of(work, struct lrw_session, timeout_work);
	lrw_st = ss->lrw_st;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);
	mutex_lock(&lrw_st->ss_list_lock);
	lrw_st->_cur_ss = NULL;
	lrw_st->state = LRW_STATE_IDLE;
	lrw_del_ss(ss);
	mutex_unlock(&lrw_st->ss_list_lock);
}

static void
rx2_timeout_isr(struct timer_list *timer)
{
	struct lrw_session *ss = container_of(timer, struct lrw_session, timer);
	struct lrw_struct *lrw_st = ss->lrw_st;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	/* Check TX is acked or not */
	if (!ss->tx_should_ack) {
		spin_lock_bh(&ss->state_lock);
		if (ss->state != LRW_RXRECEIVED_SS)
			ss->state = LRW_RXTIMEOUT_SS;
		spin_unlock_bh(&ss->state_lock);

		if (ss->state == LRW_RXTIMEOUT_SS) {
			netdev_dbg(lrw_st->ndev, "%s: rx time out\n", __func__);
			goto rx2_timeout_isr_no_retry_rx_frame;
		} else {
			return;
		}
	}

	/* Check the session need to be retransmitted or not */
	if (ss->retry > 0) {
		ss->state = LRW_RETRANSMIT_SS;
		ss->retry--;

		/* Start timer for ack timeout and retransmit */
		ss->timer.function = lrw_rexmit;
		ss->timer.expires = jiffies_64 + ss->ack_timeout * HZ;
		add_timer(&ss->timer);
	} else {
		/* Retry failed */
rx2_timeout_isr_no_retry_rx_frame:
		schedule_work(&ss->timeout_work);
	}
}

static void
rx2_delay_isr(struct timer_list *timer)
{
	struct lrw_session *ss = container_of(timer, struct lrw_session, timer);
	struct lrw_struct *lrw_st = ss->lrw_st;
	unsigned long delay;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	/* Start timer for RX2 window */
	ss->timer.function = rx2_timeout_isr;
	delay = jiffies_64 + (ss->rx2_window + 20) * HZ / 1000 + HZ;
	ss->timer.expires = delay;
	add_timer(&ss->timer);

	/* Start LoRa hardware to RX2 window */
	ss->state = LRW_RX2_SS;
	lrw_st->ops->start_rx_window(&lrw_st->hw, ss->rx2_window + 20);
}

static void
rx1_delay_isr(struct timer_list *timer)
{
	struct lrw_session *ss = container_of(timer, struct lrw_session, timer);
	struct lrw_struct *lrw_st = ss->lrw_st;
	unsigned long delay;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	/* Start timer for RX_Delay2 - RX_Delay2 */
	ss->timer.function = rx2_delay_isr;
	delay = jiffies_64 + (ss->rx_delay2 - ss->rx_delay1) * HZ;
	ss->timer.expires = delay;
	add_timer(&ss->timer);

	/* Start LoRa hardware to RX1 window */
	ss->state = LRW_RX1_SS;
	lrw_st->ops->start_rx_window(&lrw_st->hw, ss->rx1_window + 20);
}

void
lrw_sent_tx_work(struct lrw_struct *lrw_st, struct sk_buff *skb)
{
	struct lrw_session *ss = lrw_st->_cur_ss;
	struct net_device *ndev;
	unsigned long delay;

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	ss->state = LRW_XMITTED;

	/* Start session timer for RX_Delay1 */
	timer_setup(&ss->timer, rx1_delay_isr, 0);
	delay = jiffies_64 + ss->rx_delay1 * HZ - 20 * HZ / 1000;
	ss->timer.expires = delay;
	add_timer(&ss->timer);

	ndev = skb->dev;
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;
	dev_consume_skb_any(skb);
	ss->tx_skb = NULL;
}

/**
 * lrw_xmit_complete - Tell LoRaWAN module that the frame is xmitted completely
 * @hw:		the LoRa device
 * @skb:	the xmitted frame
 */
void
lrw_xmit_complete(struct lrw_hw *hw, struct sk_buff *skb)
{
	struct lrw_struct *lrw_st = container_of(hw, struct lrw_struct, hw);

	netdev_dbg(lrw_st->ndev, "%s\n", __func__);

	lrw_sent_tx_work(lrw_st, skb);
	lrw_st->state = LRW_STATE_RX;
}
EXPORT_SYMBOL(lrw_xmit_complete);
