/* SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause */
/*
 * LoRaWAN crypto APIs
 *
 * Copyright (c) 2018 Jian-Hong, Pan <starnight@g.ncu.edu.tw>
 */

#ifndef __LORAWAN_CRYPTO_H__
#define __LORAWAN_CRYPTO_H__

#include <crypto/hash.h>
#include <crypto/skcipher.h>

struct crypto_shash *lrw_mic_key_setup(u8 *k, size_t k_len);
int lrw_calc_mic(struct crypto_shash *tfm,
		 u8 dir, u32 devaddr, u32 fcnt, u8 *buf, size_t len, u8 *mic4);
void lrw_mic_key_free(struct crypto_shash *tfm);

struct crypto_sync_skcipher *lrw_encrypt_key_setup(u8 *k, size_t k_len);
int lrw_encrypt_buf(struct crypto_sync_skcipher *tfm,
		    u8 dir, u32 devaddr, u32 fcnt, u8 *buf, size_t len);
int lrw_decrypt_buf(struct crypto_sync_skcipher *tfm,
		    u8 dir, u32 devaddr, u32 fcnt, u8 *buf, size_t len);
void lrw_encrypt_key_free(struct crypto_sync_skcipher *tfm);

#endif
