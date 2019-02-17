/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause */
/*
 * LoRaWAN compatible hardware's definitions
 *
 * Copyright (c) 2018 Jian-Hong, Pan <starnight@g.ncu.edu.tw>
 */

#ifndef __LORAWAN_H__
#define __LORAWAN_H__

#include <linux/skbuff.h>

/* List the role of the LoRaWAN hardware */
enum {
	LRW_GATEWAY,
	LRW_CLASS_A_NODE,
	LRW_CLASS_B_NODE,
	LRW_CLASS_C_NODE,
};

/* List the RF modes */
enum {
	LRW_LORA,
	LRW_FSK,
};

/**
 * lrw_dr - This structure holds the RF related configuration of the data rate.
 * @bw:
 *	Bandwidth in Hz
 *
 * @sf:
 *	Spread factor of CSS modulation used by LoRa mode
 *
 * @mode:
 *	LoRa or FSK mode
 */
struct lrw_dr {
	u32 bw;
	u8 sf;
	u8 mode;
};

#define	LRW_DEVADDR_LEN		(sizeof(__le32))

/* List the LoRa device's states of LoRaWAN hardware */
enum {
	LRW_STOP,
	LRW_START,
	LRW_STATE_IDLE,
	LRW_STATE_TX,
	LRW_STATE_RX,
};

/**
 * lrw_hw - This structure holds the LoRa device of LoRaWAN hardware.
 * @priv:
 *	points to the private data of the LoRa device
 */
struct lrw_hw {
	void *priv;
};

/**
 * lrw_operations - Lists the LoRaWAN device/interface's operations.
 * These are callback functions for the LoRaWAN module.  Compatible LoRa device
 * driver should implement some of them according to the usage.  The
 * unimplemented callback functions must be assigned as NULL.
 *
 * @start:
 *	called when the interface is being up state
 *
 * @stop:
 *	called when the interface is being down state
 *
 * @xmit_async:
 *	called to xmit the data through the interface asynchronously
 *
 * @set_txpower:
 *	called to set xmitting RF power in mBm of the interface
 *
 * @set_frq:
 *	called to set carrier frequency in Hz of the interface
 *
 * @set_dr:
 *	called to set related RF configuration of the LoRaWAN data rate
 *
 * @start_rx_window:
 *	called to ask the LoRa device open a receiving window
 *
 * @set_state:
 *	called to set the LoRa device's working state
 */
struct lrw_operations {
	int (*start)(struct lrw_hw *hw);
	void (*stop)(struct lrw_hw *hw);

	int (*xmit_async)(struct lrw_hw *hw, struct sk_buff *skb);
	int (*set_txpower)(struct lrw_hw *hw, s32 pwr);
	int (*set_frq)(struct lrw_hw *hw, u32 frq);
	int (*set_dr)(struct lrw_hw *hw, struct lrw_dr *dr);
	int (*start_rx_window)(struct lrw_hw *hw, u32 delay);
	int (*set_state)(struct lrw_hw *hw, u8 state);
};

struct lrw_hw *lrw_alloc_hw(size_t priv_data_len, struct lrw_operations *ops);
void lrw_free_hw(struct lrw_hw *hw);
int lrw_register_hw(struct lrw_hw *hw);
void lrw_unregister_hw(struct lrw_hw *hw);
void lrw_rx_irqsave(struct lrw_hw *hw, struct sk_buff *skb);
void lrw_xmit_complete(struct lrw_hw *hw, struct sk_buff *skb);

void lrw_set_deveui(struct lrw_hw *hw, u64 eui);
u64 lrw_get_deveui(struct lrw_hw *hw);
void lrw_set_appeui(struct lrw_hw *hw, u64 eui);
u64 lrw_get_appeui(struct lrw_hw *hw);
void lrw_set_devaddr(struct lrw_hw *hw, u32 eui);
u32 lrw_get_devaddr(struct lrw_hw *hw);

enum {
	LRW_APPKEY,
	LRW_NWKSKEY,
	LRW_APPSKEY,
};

#define	LRW_KEY_LEN		16

int lrw_set_key(struct lrw_hw *hw, u8 type, u8 *key, size_t key_len);

#endif
