/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause */
/*
 * LoRaWAN soft MAC
 *
 * Copyright (c) 2018 Jian-Hong, Pan <starnight@g.ncu.edu.tw>
 */

#ifndef __MAC_LORAWAN_H__
#define __MAC_LORAWAN_H__

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <uapi/linux/if_arp.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/lora/lorawan.h>

#define	LORAWAN_MODULE_NAME	"maclorawan"

/* List the message types of LoRaWAN */
enum {
	LRW_JOIN_REQUEST,
	LRW_JOIN_ACCEPT,
	LRW_UNCONFIRMED_DATA_UP,
	LRW_UNCONFIRMED_DATA_DOWN,
	LRW_CONFIRMED_DATA_UP,
	LRW_CONFIRMED_DATA_DOWN,
	LRW_RFU,
	LRW_PROPRIETARY,
};

/* List the communication directions */
enum {
	LRW_UPLINK,
	LRW_DOWNLINK,
};

/* States of LoRaWAN slot timing */
enum {
	LRW_INIT_SS,
	LRW_XMITTING_SS,
	LRW_XMITTED,
	LRW_RX1_SS,
	LRW_RX2_SS,
	LRW_RXTIMEOUT_SS,
	LRW_RXRECEIVED_SS,
	LRW_RETRANSMIT_SS,
};

#define	LRW_MHDR_LEN		1
#define	LRW_FHDR_MAX_LEN	22
#define	LRW_FCTRL_LEN		1
#define	LRW_FCNT_LEN		2
#define	LRW_FOPS_MAX_LEN	15
#define	LRW_FPORT_LEN		1
#define	LRW_MIC_LEN		4

/**
 * lrw_fhdr - Hold the message's basic information of the frame
 *
 * @mtype:		this message's type
 * @fctrl:		the frame control byte
 * @fcnt:		this message's frame counter value
 * @fopts:		this frame's options field
 * @fopts_len:		the length of the fopts
 */
struct lrw_fhdr {
	u8 mtype;
	u8 fctrl;
	u16 fcnt;
	u8 fopts[LRW_FPORT_LEN];
	u8 fopts_len;
};

/**
 * lrw_session - LoRaWAN session for Class A end device
 *
 * @lrw_st:		points to the belonging lrw_st
 * @entry:		the entry of the ss_list in lrw_struct
 * @devaddr:		the LoRaWAN device address of this LoRaWAN hardware
 * @fcnt_up:		uplink frame counter
 * @fcnt_down:		downlink frame counter
 * @fport:		the LoRaWAN data message's port field
 * @tx_skb:		points to the TX skb, the frame
 * @rx_skb:		points to the RX skb, the frame
 * @tx_fhdr:		hold the message's basic information of the TX frame
 * @rx_fhdr:		hold the message's basic information of the RX frame
 * @tx_should_ack:	flag for determining the TX which should be acked or not
 * @retry:		retry times for xmitting failed
 * @state:		this session's current state
 * @state_lock:		lock of the session's state
 * @timer:		timing for this session and the state transition
 * @timeout_work:	work if waiting acknowledge time out
 * @rx_delay1:		RX1 delay time in seconds
 * @rx_delay2:		RX2 delay time in seconds
 * @rx1_window:		RX1 window opening time in milliseconds
 * @rx2_window:		RX2 window opening time in milliseconds
 * @ack_timeout:	time out time for waiting acknowledge in seconds
 */
struct lrw_session {
	struct lrw_struct *lrw_st;
	struct list_head entry;

	u32 devaddr;
	u16 fcnt_up;
	u16 fcnt_down;
	u8 fport;
	struct sk_buff *tx_skb;
	struct sk_buff *rx_skb;
	struct lrw_fhdr tx_fhdr;
	struct lrw_fhdr rx_fhdr;

	u8 tx_should_ack;
	u8 retry;
	u8 state;
	spinlock_t state_lock; /* lock of the session's state */

	struct timer_list timer;
	struct work_struct timeout_work;
	unsigned long rx_delay1;
	unsigned long rx_delay2;
	unsigned long rx1_window;
	unsigned long rx2_window;
	unsigned long ack_timeout;
};

/**
 * lrw_struct - The full LoRaWAN hardware to the LoRa device.
 *
 * @dev:		this LoRa device registed in system
 * @hw:			the LoRa device of this LoRaWAN hardware
 * @ops:		handle of LoRa operations interfaces
 * @rx_skb_list:	the list of received frames
 * @ss_list:		LoRaWAN session list of this LoRaWAN hardware
 * @ss_list_lock:	lock of the session list
 * @_cur_ss:		pointer of the current processing session
 * @rx_should_ack:	represent the current session should be acked or not
 * @state:		the state of this LoRaWAN hardware
 * @app_eui:		the LoRaWAN application EUI
 * @dev_eui:		the LoRaWAN device EUI
 * @devaddr:		the LoRaWAN device address of this LoRaWAN hardware
 * @appky:		the Application key
 * @nwkskey:		the Network session key
 * @appskey:		the Application session key
 * @nwks_shash_tfm:	the hash handler for LoRaWAN network session
 * @nwks_skc_tfm:	the crypto handler for LoRaWAN network session
 * @apps_skc_tfm:	the crypto handler for LoRaWAN application session
 * @fcnt_up:		the counter of this LoRaWAN hardware's up frame
 * @fcnt_down:		the counter of this LoRaWAN hardware's down frame
 * @xmit_task:		the xmit task for the current LoRaWAN session
 * @rx_work:		the RX work in workqueue for the current LoRaWAN session
 * @ndev:		points to the emulating network device
 * @_net:		the current network namespace of this LoRaWAN hardware
 */
struct lrw_struct {
	struct device dev;
	struct lrw_hw hw;
	struct lrw_operations *ops;

	struct sk_buff_head rx_skb_list;
	struct list_head ss_list;
	struct mutex ss_list_lock; /* lock of the session list */
	struct lrw_session *_cur_ss;
	u8 rx_should_ack;
	u8 state;

	u64 app_eui;
	u64 dev_eui;
	u32 devaddr;
	u8 appkey[LRW_KEY_LEN];
	u8 nwkskey[LRW_KEY_LEN];
	u8 appskey[LRW_KEY_LEN];
	struct crypto_shash *nwks_shash_tfm;
	struct crypto_sync_skcipher *nwks_skc_tfm;
	struct crypto_sync_skcipher *apps_skc_tfm;

	u16 fcnt_up;
	u16 fcnt_down;

	struct tasklet_struct xmit_task;
	struct work_struct rx_work;

	struct net_device *ndev;
	possible_net_t _net;
};

#define	NETDEV_2_LRW(ndev)	((struct lrw_struct *)netdev_priv(ndev))

struct lrw_session *lrw_alloc_ss(struct lrw_struct *lrw_st);
void lrw_free_ss(struct lrw_session *ss);
void lrw_del_ss(struct lrw_session *ss);
int lrw_start_hw(struct lrw_struct *lrw_st);
void lrw_stop_hw(struct lrw_struct *lrw_st);
void lrw_prepare_tx_frame(struct lrw_session *ss);
void lrw_xmit(unsigned long data);
void lrw_rx_work(struct work_struct *work);

#endif
