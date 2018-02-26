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

#include "nrf.h"

/** @brief Enable CC310 hardware. */
__STATIC_INLINE void cc310_enable(void)
{
    NRF_CRYPTOCELL->ENABLE = 1;
}

/** @brief Disable CC310 hardware. */
__STATIC_INLINE void cc310_disable(void)
{
    NRF_CRYPTOCELL->ENABLE = 0;
    NVIC_ClearPendingIRQ(CRYPTOCELL_IRQn);
}

/** @brief Wrapper for CC310 operations which ignores result.
 *
 *  This macro enables CC310 hardware before operation and disables it after operation.
 *  Result of the CC310 operation is ignored.
 *
 *  @param[in] operation CC310 operation to execute.
 */
#define CC310_OPERATION_NO_RESULT(operation)    \
    do                                          \
    {                                           \
        cc310_enable();                         \
        (void)operation;                        \
        cc310_disable();                        \
    } while (0)

/** @brief Wrapper for CC310 operations.
 *
 *  This macro enables CC310 hardware before operation and disables it after operation.
 *  Result of the CC310 operation is returned through a variable passed to the macro.
 *
 *  @param[in]  operation CC310 operation to execute.
 *  @param[out] result    Variable to store result of the operation.
 */
#define CC310_OPERATION(operation, result)      \
    do                                          \
    {                                           \
        cc310_enable();                         \
        result = operation;                     \
        cc310_disable();                        \
    } while (0)
