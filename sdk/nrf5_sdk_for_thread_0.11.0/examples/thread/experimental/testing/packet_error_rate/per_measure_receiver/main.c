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
/** @file
 *
 * @defgroup test_per_measure_receiver_example_main main.c
 * @{
 * @ingroup test_per_measure_receiver_example
 * @brief An example presenting OpenThread UDP messages receiver for packet error rate (PER) measurement.
 *
 * @details This test application with related per_measure_transmitter enables to measure Packet Error Rate
 *          between two Thread devices. After establishing connection via CoAP, this application counts
 *          test packets received on UDP from the transmitter node. When the test is finished, the application
 *          provides related transmitter node with two values: the number of incorrectly received UDP packets
 *          (packet error rate on the application layer) and the number of incorrectly received frames
 *          in relation to all received frames (packet error rate on the MAC layer).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_timer.h"
#include "bsp_thread.h"
#include "nrf_assert.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "thread_utils.h"

#include <openthread/config.h>
#include <openthread/diag.h>
#include <openthread/openthread.h>
#include <openthread/cli.h>
#include <openthread/platform/platform.h>
#include <openthread/udp.h>
#include <openthread/coap.h>

#define IPV6_ADDR_BYTE_LENGTH 16                /**< Length of IPv6 address in bytes. */
#define UDP_PORT              40004             /**< Default UDP port. */

/**@brief Forward declaration of a function called when request for local IPv6 address is handled. */
static void coap_addr_request_handler(void                * p_context,
                                      otCoapHeader        * p_header,
                                      otMessage           * p_message,
                                      const otMessageInfo * p_message_info);

/**@brief Forward declaration of a function called when request for test results is handled. */
static void coap_test_request_handler(void                * p_context,
                                      otCoapHeader        * p_header,
                                      otMessage           * p_message,
                                      const otMessageInfo * p_message_info);

typedef struct node_remote_t
{
    otIp6Address addr;                          /**< An IPv6 address. */
    uint16_t     port;                          /**< A port number. */
} node_remote_t;

typedef struct test_result_t
{
    uint32_t rx_total;                          /**< Total number of correctly received frames. */
    uint32_t rx_error;                          /**< Total number of incorrectly received frames. */
    uint32_t udp_total;                         /**< Total number of UDP test packets received. */
} test_result_t;

typedef struct
{
    otUdpSocket    socket;                      /**< An OpenThread UDP Socket. */
    otCoapResource result_resource;             /**< An OpenThread CoAP test result resource. */
    otCoapResource addr_resource;               /**< An OpenThread CoAP local IPv6 address resource. */
    node_remote_t  peer;                        /**< Peer (transmitter node) address. */
    test_result_t  result;                      /**< Results of a test. */
} application_t;

static application_t m_app;

/***************************************************************************************************
 * @section UDP
 **************************************************************************************************/

/**@brief This function is called when UDP message is handled.
 *
 * @param[inout] p_context       Pointer to application specific context; here NULL.
 * @param[in]    p_message       Pointer to OpenThread message buffer.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 */
static void udp_receive_callback(void                * p_context,
                                 otMessage           * p_message,
                                 const otMessageInfo * p_message_info)
{
    UNUSED_VARIABLE(p_context);
    UNUSED_VARIABLE(p_message);
    UNUSED_VARIABLE(p_message_info);

    m_app.result.udp_total += 1;
}


/**@brief Function for initializing a UDP port.
 *
 * @details This functions opens and binds an OpenThread UDP socket.
 */
static void udp_port_create(void)
{
    otError    error = OT_ERROR_NONE;
    otSockAddr addr;
    memset(&addr, 0, sizeof(addr));

    error = otUdpOpen(thread_ot_instance_get(), &m_app.socket, udp_receive_callback, NULL);
    if (error == OT_ERROR_NONE)
    {
        addr.mPort = UDP_PORT;
        error      = otUdpBind(&m_app.socket, &addr);
    }

    if (error != OT_ERROR_NONE)
    {
        NRF_LOG_INFO("Failed to create UDP port: %d\r\n", error);
    }

    ASSERT(error == OT_ERROR_NONE);
}

/***************************************************************************************************
 * @section CoAP
 **************************************************************************************************/

/**@brief Function for initializing test result values. */
static void test_results_init(void)
{
    otMacCounters counters = *otLinkGetCounters(thread_ot_instance_get());

    m_app.result.rx_total  = counters.mRxTotal;
    m_app.result.rx_error  = counters.mRxErrNoFrame + counters.mRxErrFcs +
                             counters.mRxErrSec + counters.mRxErrOther;
    m_app.result.udp_total = 0;
}


/**@brief Function for filling a buffer with test results.
 */
static void test_results_fill(void)
{
    otMacCounters counters = *otLinkGetCounters(thread_ot_instance_get());

    uint32_t rx_total = counters.mRxTotal;
    uint32_t rx_error = counters.mRxErrNoFrame + counters.mRxErrFcs +
                        counters.mRxErrSec + counters.mRxErrOther;

    m_app.result.rx_total  = rx_total - m_app.result.rx_total;
    m_app.result.rx_error  = rx_error - m_app.result.rx_error;
}


/**@brief Function for sending a response to a local IPv6 address request.
 *
 * @param[in]    p_context       Pointer to application specific context; here NULL.
 * @param[in]    p_header        Pointer to OpenThread CoAP message header.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 */
static void coap_addr_response_send(void                * p_context,
                                    otCoapHeader        * p_request_header,
                                    const otMessageInfo * p_message_info)
{
    otError      error = OT_ERROR_NO_BUFS;
    otCoapHeader header;
    otMessage *  p_response;

    do
    {
        otCoapHeaderInit(&header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_CONTENT);
        otCoapHeaderSetToken(&header,
                             otCoapHeaderGetToken(p_request_header),
                             otCoapHeaderGetTokenLength(p_request_header));
        otCoapHeaderSetPayloadMarker(&header);

        p_response = otCoapNewMessage(p_context, &header);
        if (p_response == NULL)
        {
            break;
        }

        uint16_t udp_port_receiver = UDP_PORT;

        error = otMessageAppend(p_response, &udp_port_receiver, sizeof(uint16_t));
        if (error != OT_ERROR_NONE)
        {
            break;
        }

        error = otCoapSendResponse(p_context, p_response, p_message_info);

    } while (false);

    if (error != OT_ERROR_NONE && p_response != NULL)
    {
        otMessageFree(p_response);
    }
}


/**@brief Function for handling a CoAP request for local IPv6 address.
 *
 * @param[inout] p_context       Pointer to application specific context; here pointer to OpenThread instance.
 * @param[in]    p_header        Pointer to OpenThread CoAP message header.
 * @param[in]    p_message       Pointer to OpenThread message buffer.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 */
static void coap_addr_request_handler(void                * p_context,
                                      otCoapHeader        * p_header,
                                      otMessage           * p_message,
                                      const otMessageInfo * p_message_info)
{
    UNUSED_VARIABLE(p_message);

    otMessageInfo message_info;

    if (otCoapHeaderGetType(p_header) == OT_COAP_TYPE_NON_CONFIRMABLE &&
        otCoapHeaderGetCode(p_header) == OT_COAP_CODE_GET)
    {
        message_info = *p_message_info;
        memset(&message_info.mSockAddr, 0, sizeof(message_info.mSockAddr));
        message_info.mSockAddr = *otThreadGetMeshLocalEid(thread_ot_instance_get());

        coap_addr_response_send(p_context, p_header, &message_info);

        memcpy(&m_app.peer.addr, p_message_info->mPeerAddr.mFields.m8, OT_IP6_ADDRESS_SIZE);
        m_app.peer.port = p_message_info->mPeerPort;

        test_results_init();
    }
}


/**@brief Function for sending a response to test results request.
 *
 * @param[in]    p_context       Pointer to application specific context; here NULL.
 * @param[in]    p_header        Pointer to OpenThread CoAP message header.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 */
static void coap_test_response_send(void                * p_context,
                                    otCoapHeader        * p_request_header,
                                    const otMessageInfo * p_message_info)
{
    otError      error = OT_ERROR_NO_BUFS;
    otCoapHeader header;
    otMessage  * p_response;

    do
    {
        otCoapHeaderInit(&header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_CONTENT);
        otCoapHeaderSetToken(&header,
                             otCoapHeaderGetToken(p_request_header),
                             otCoapHeaderGetTokenLength(p_request_header));
        otCoapHeaderSetPayloadMarker(&header);

        p_response = otCoapNewMessage(p_context, &header);
        if (p_response == NULL)
        {
            break;
        }

        test_results_fill();

        error = otMessageAppend(p_response, &m_app.result, sizeof(m_app.result));
        if (error != OT_ERROR_NONE)
        {
            break;
        }

        error = otCoapSendResponse(p_context, p_response, p_message_info);

    } while (false);

    if (error != OT_ERROR_NONE && p_response != NULL)
    {
        otMessageFree(p_response);
    }
}


/**@brief Function for handling a CoAP request for test results.
 *
 * @param[inout] p_context       Pointer to application specific context; here pointer to OpenThread instance.
 * @param[in]    p_header        Pointer to OpenThread CoAP message header.
 * @param[in]    p_message       Pointer to OpenThread message buffer.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 */
static void coap_test_request_handler(void                * p_context,
                                      otCoapHeader        * p_header,
                                      otMessage           * p_message,
                                      const otMessageInfo * p_message_info)
{
    UNUSED_VARIABLE(p_message);

    if (otCoapHeaderGetType(p_header) == OT_COAP_TYPE_NON_CONFIRMABLE &&
        otCoapHeaderGetCode(p_header) == OT_COAP_CODE_GET)
    {
        coap_test_response_send(p_context, p_header, p_message_info);
    }

}

/***************************************************************************************************
 * @section State
 **************************************************************************************************/

static void state_changed_callback(uint32_t flags, void * p_context)
{
    NRF_LOG_INFO("State changed! Flags: 0x%08x Current role: %d\r\n",
                 flags, otThreadGetDeviceRole(p_context));
}

/***************************************************************************************************
 * @section Initialization
 **************************************************************************************************/

/**@brief Function for initializing the Application Timer Module.
 */
static void timer_init(void)
{
    uint32_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the LEDs.
 */
static void leds_init(void)
{
    LEDS_CONFIGURE(LEDS_MASK);
    LEDS_OFF(LEDS_MASK);
}


/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}


/**@brief Function for initializing the Thread Board Support Package.
 */
static void thread_bsp_init(void)
{
    uint32_t err_code;
    err_code = bsp_init(BSP_INIT_LED | BSP_INIT_BUTTONS, NULL);
    APP_ERROR_CHECK(err_code);

    err_code = bsp_thread_init(thread_ot_instance_get());
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Thread Stack.
 */
static void thread_instance_init(void)
{
    thread_configuration_t thread_configuration =
    {
        .role              = RX_ON_WHEN_IDLE,
        .autocommissioning = true,
    };

    thread_init(&thread_configuration);
    thread_cli_init();
    thread_state_changed_callback_set(state_changed_callback);
}


/**@brief Function for initializing the CoAP module.
 */
static void coap_init(void)
{
    m_app.result_resource.mUriPath = "result";
    m_app.result_resource.mHandler = coap_test_request_handler;
    m_app.result_resource.mContext = thread_ot_instance_get();

    m_app.addr_resource.mUriPath = "addr";
    m_app.addr_resource.mHandler = coap_addr_request_handler;
    m_app.addr_resource.mContext = thread_ot_instance_get();

    otError error = otCoapStart(thread_ot_instance_get(), OT_DEFAULT_COAP_PORT);
    ASSERT(error == OT_ERROR_NONE);

    error = otCoapAddResource(thread_ot_instance_get(), &m_app.result_resource);
    ASSERT(error == OT_ERROR_NONE);

    error = otCoapAddResource(thread_ot_instance_get(), &m_app.addr_resource);
    ASSERT(error == OT_ERROR_NONE);
}

/**@brief Function for initializing the test.
 */
static void test_init(void)
{
    udp_port_create();
}

/***************************************************************************************************
 * @section Main
 **************************************************************************************************/

int main(int argc, char *argv[])
{
    log_init();
    timer_init();
    leds_init();

    thread_instance_init();
    thread_bsp_init();
    coap_init();
    test_init();

    while (true)
    {
        thread_process();

        if (NRF_LOG_PROCESS() == false)
        {
            thread_sleep();
        }
    }
}

/**
 *@}
 **/
