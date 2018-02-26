/**
 * Copyright (c) 2017 - 2017, Nordic Semiconductor ASA
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 * 
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 * 
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 * 
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 * 
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef MBEDTLS_AES_ALT_H
#define MBEDTLS_AES_ALT_H

#if !defined(MBEDTLS_CONFIG_FILE)
#include "config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef MBEDTLS_AES_ALT

#include "ssi_aes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AES context structure
 */
typedef struct
{
    SaSiAesUserContext_t user_context;   ///< User context for CC310 AES.
    uint8_t              key_buffer[32]; ///< Buffer for an encryption key.
    SaSiAesUserKeyData_t key;            ///< CC310 AES key structure.
    SaSiAesEncryptMode_t mode;           ///< Current context operation mode (encrypt/decrypt).
}
mbedtls_aes_context;

/**
 * @brief Initialize AES context
 *
 * @param[in,out] ctx AES context to be initialized
 */
void mbedtls_aes_init(mbedtls_aes_context * ctx);

/**
 * @brief Clear AES context
 *
 * @param[in,out] ctx AES context to be cleared
 */
void mbedtls_aes_free(mbedtls_aes_context * ctx);

/**
 * @brief AES key schedule (encryption)
 *
 * @param[in,out] ctx     AES context to be initialized
 * @param[in]     key     encryption key
 * @param[in]     keybits must be 128, 192 or 256
 *
 * @return 0 if successful
 */
int mbedtls_aes_setkey_enc(mbedtls_aes_context * ctx,
                           const unsigned char * key,
                           unsigned int          keybits);

/**
 * @brief AES key schedule (decryption)
 *
 * @param[in,out] ctx     AES context to be initialized
 * @param[in]     key     decryption key
 * @param[in]     keybits must be 128, 192 or 256
 *
 * @return 0 if successful
 */
int mbedtls_aes_setkey_dec(mbedtls_aes_context * ctx,
                           const unsigned char * key,
                           unsigned int          keybits);

/**
 * @brief AES-ECB block encryption/decryption
 *
 * @param[in,out] ctx    AES context
 * @param[in]     mode   MBEDTLS_AES_ENCRYPT or MBEDTLS_AES_DECRYPT
 * @param[in]     input  16-byte input block
 * @param[out]    output 16-byte output block
 *
 * @return 0 if successful
 */
int mbedtls_aes_crypt_ecb(mbedtls_aes_context * ctx,
                          int                   mode,
                          const unsigned char   input[16],
                          unsigned char         output[16]);

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_AES_ALT */

#endif /* MBEDTLS_AES_ALT_H */
