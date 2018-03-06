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
 * @defgroup test_per_measure_transmitter_example_main main.c
 * @{
 * @ingroup test_per_measure_transmitter_example
 * @brief An example presenting OpenThread UDP messages transmitter for packet error rate (PER) measurement.
 *
 * @details This test application with related per_measure_receiver enables to measure Packet Error Rate
 *          between two Thread devices. After establishing connection via CoAP, this application sends
 *          a number of test packets on UDP. When the test is finished, two values are acquired from the
 *          receiver node: the number of incorrectly received UDP packets (packet error rate on
 *          application layer) and the number of incorrectly received frames in relation to all received
 *          frames (packet error rate on MAC layer).
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

#define IPV6_ADDR_BYTE_LENGTH   16                /**< Length of IPv6 address in bytes. */
#define PAYLOAD_BYTE_LENGTH     4                 /**< Length of test packet payload in bytes. */
#define TEST_RESULTS_LENGTH     16                /**< Length of test results in bytes. */
#define UDP_PORT                40001             /**< Default UDP port of the transmitter. */
#define NUMBER_OF_TEST_PACKETS  1000              /**< Number of test UDP packets to send. */
#define TEST_PACKET_SEND_PERIOD 10                /**< Period between UDP test packets [ms]. */
#define COAP_TOKEN_LENGTH       2                 /**< Length of a CoAP token. */
#define IPV6_MCAST_ADDR_STRING  "FF03::1"         /**< IPv6 multicast address in string format. */

APP_TIMER_DEF(m_timer);

/**@brief Forward declaration of a function handling timer timeout. */
static void timeout_handler(void * p_context);

typedef struct node_remote_t
{
    otIp6Address addr;                            /**< IPv6 address. */
    uint16_t     port;                            /**< Port number. */
} node_remote_t;

typedef struct test_result_t
{
    uint32_t rx_total;                            /**< Total number of correctly received frames. */
    uint32_t rx_error;                            /**< Total number of incorrectly received frames. */
    uint32_t udp_total;                           /**< Total number of UDP test packets received. */
} test_result_t;

typedef struct
{
    otUdpSocket   socket;                         /**< OpenThread UDP Socket. */
    node_remote_t peer;                           /**< Peer (receiver node) address. */
    uint32_t      test_packet_counter;            /**< Counter of test packets. */
} application_t;

static application_t m_app;
static volatile bool m_timer_task_pending;

/***************************************************************************************************
 * @section UDP
 **************************************************************************************************/

/**@brief Function for initializing a UDP port.
 *
 * @details This functions opens and binds an OpenThread UDP socket.
 */
static void udp_port_create(void)
{
    otError    error = OT_ERROR_NONE;
    otSockAddr addr;
    memset(&addr, 0, sizeof(addr));

    error = otUdpOpen(thread_ot_instance_get(), &m_app.socket, NULL, NULL);
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


/**@brief Function for sending a UDP test packet. */
static otError udp_packet_transmit(void)
{
    otError     err_code  = OT_ERROR_NO_BUFS;
    otMessage * p_message = NULL;
    uint8_t     p_payload[PAYLOAD_BYTE_LENGTH];
    memcpy(p_payload, &m_app.test_packet_counter, sizeof(m_app.test_packet_counter));

    do
    {
        p_message = otUdpNewMessage(thread_ot_instance_get(), true);
        if (p_message == NULL)
        {
            NRF_LOG_ERROR("Failed to allocate message for UDP port.\r\n");
            break;
        }

        if ((err_code = otMessageAppend(p_message, p_payload, PAYLOAD_BYTE_LENGTH)) != OT_ERROR_NONE)
        {
            NRF_LOG_ERROR("Failed to append message payload for UDP port.\r\n");
            break;
        }

        otMessageInfo message_info;

		memset(&message_info, 0, sizeof(message_info));
		message_info.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
		message_info.mPeerPort    = m_app.peer.port;

		memcpy(message_info.mPeerAddr.mFields.m8, &m_app.peer.addr, OT_IP6_ADDRESS_SIZE);
        message_info.mSockAddr = *otThreadGetMeshLocalEid(thread_ot_instance_get());

        if ((err_code = otUdpSend(&m_app.socket, p_message, &message_info)) != OT_ERROR_NONE)
        {
            NRF_LOG_ERROR("Failed to send UDP message.\r\n");
            break;
        }

    } while(0);

    if ((p_message != NULL) && (err_code != OT_ERROR_NONE))
	{
		otMessageFree(p_message);
    }

    return err_code;
}

/***************************************************************************************************
 * @section CoAP
 **************************************************************************************************/

/**@brief Function for handling a response sent to a peer IPv6 request.
 *
 * @param[inout] p_context       Pointer to application specific context; here unused.
 * @param[in]    p_header        Pointer to OpenThread CoAP message header.
 * @param[in]    p_message       Pointer to OpenThread message buffer.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 * @param[in]    result          A result of the OpenThread CoAP transaction.
 */
static void coap_addr_response_handler(void                * p_context,
                                       otCoapHeader        * p_header,
                                       otMessage           * p_message,
                                       const otMessageInfo * p_message_info,
                                       otError               result)
{
    UNUSED_VARIABLE(p_context);

    if (otMessageRead(p_message,
                      otMessageGetOffset(p_message),
                      &m_app.peer.port,
                      sizeof(m_app.peer.port)) == sizeof(m_app.peer.port))
    {
        memcpy(&m_app.peer.addr, p_message_info->mPeerAddr.mFields.m8, OT_IP6_ADDRESS_SIZE);
        m_app.test_packet_counter = 0;

        NRF_LOG_INFO("PER test has been started. Please wait for the results.\r\n");

        uint32_t err_code = app_timer_start(m_timer, APP_TIMER_TICKS(TEST_PACKET_SEND_PERIOD), NULL);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for sending OpenThread CoAP request for a peer IPv6 address. */
static void coap_addr_request_send(void)
{
    otError       error = OT_ERROR_NONE;
    otMessage   * p_message;
    otMessageInfo messageInfo;
    otCoapHeader  header;

    do
    {
        otCoapHeaderInit(&header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_GET);
        otCoapHeaderGenerateToken(&header, COAP_TOKEN_LENGTH);
        UNUSED_VARIABLE(otCoapHeaderAppendUriPathOptions(&header, "addr"));

        p_message = otCoapNewMessage(thread_ot_instance_get(), &header);
        if (p_message == NULL)
        {
            NRF_LOG_INFO("Failed to allocate message for CoAP Request\r\n");
            break;
        }

        memset(&messageInfo, 0, sizeof(messageInfo));
        messageInfo.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
        messageInfo.mSockAddr    = *otThreadGetMeshLocalEid(thread_ot_instance_get());
        messageInfo.mPeerPort    = OT_DEFAULT_COAP_PORT;
        UNUSED_VARIABLE(otIp6AddressFromString(IPV6_MCAST_ADDR_STRING, &messageInfo.mPeerAddr));

        error = otCoapSendRequest(thread_ot_instance_get(),
                                  p_message,
                                  &messageInfo,
                                  coap_addr_response_handler,
                                  NULL);
    } while (false);

    if (error != OT_ERROR_NONE && p_message != NULL)
    {
        NRF_LOG_INFO("Failed to send CoAP Request: %d\r\n", error);
        otMessageFree(p_message);
    }
}


/**@brief Function for handling a response sent to test results request.
 *
 * @param[inout] p_context       Pointer to application specific context; here unused.
 * @param[in]    p_header        Pointer to OpenThread CoAP message header.
 * @param[in]    p_message       Pointer to OpenThread message buffer.
 * @param[in]    p_message_info  Pointer to OpenThread message information.
 * @param[in]    result          A result of the OpenThread CoAP transaction.
 */
static void coap_test_result_response_handler(void                * p_context,
                                              otCoapHeader        * p_header,
                                              otMessage           * p_message,
                                              const otMessageInfo * p_message_info,
                                              otError               result)
{
    UNUSED_VARIABLE(p_context);
    UNUSED_VARIABLE(p_header);

    test_result_t test_results;

    if (result == OT_ERROR_NONE)
    {
        if (otMessageRead(p_message, otMessageGetOffset(p_message), &test_results, sizeof(test_results)) ==
            sizeof(test_results))
        {
            uint32_t udp_lost = NUMBER_OF_TEST_PACKETS - test_results.udp_total;
            int udp_per       = (10000 * udp_lost / NUMBER_OF_TEST_PACKETS);

            NRF_LOG_INFO("\r\n\r\n\tPACKET ERROR RATE MEASUREMENTS: \r\n\r\n"
                         "\tTotal number of received UDP packets: %d\r\n"
                         "\tTotal number of lost UDP packets: %d\r\n"
                         "\tPacket error rate: %d.%02d\r\n\r\n"
                         "\tTotal number of received MAC frames: %d\r\n"
                         "\tTotal number of incorrectly received MAC frames: %d\r\n",
                         test_results.udp_total, udp_lost, (udp_per / 100), (udp_per % 100),
                         test_results.rx_total, test_results.rx_error);
        }
    }
}


/**@brief Function for sending an OpenThread CoAP request for test results. */
static void coap_test_result_request_send(void)
{
    otError       error = OT_ERROR_NONE;
    otMessage   * p_message;
    otMessageInfo messageInfo;
    otCoapHeader  header;

    do
    {
        otCoapHeaderInit(&header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_GET);
        otCoapHeaderGenerateToken(&header, COAP_TOKEN_LENGTH);
        UNUSED_VARIABLE(otCoapHeaderAppendUriPathOptions(&header, "result"));

        p_message = otCoapNewMessage(thread_ot_instance_get(), &header);
        if (p_message == NULL)
        {
            NRF_LOG_INFO("Failed to allocate message for CoAP Request\r\n");
            break;
        }

        memset(&messageInfo, 0, sizeof(messageInfo));
        messageInfo.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
        messageInfo.mPeerPort    = OT_DEFAULT_COAP_PORT;
        memcpy(&messageInfo.mPeerAddr, &m_app.peer.addr, sizeof(messageInfo.mPeerAddr));

        error = otCoapSendRequest(thread_ot_instance_get(),
                                  p_message,
                                  &messageInfo,
                                  coap_test_result_response_handler,
                                  thread_ot_instance_get());
    } while (false);

    if (error != OT_ERROR_NONE && p_message != NULL)
    {
        NRF_LOG_INFO("Failed to send CoAP Request: %d\r\n", error);
        otMessageFree(p_message);
    }
}

/***************************************************************************************************
 * @section Timer
 **************************************************************************************************/

static void timeout_process(void)
{
    if (!m_timer_task_pending)
    {
        return;
    }

    m_timer_task_pending = false;

    if (m_app.test_packet_counter < NUMBER_OF_TEST_PACKETS)
    {
        if (udp_packet_transmit() == OT_ERROR_NONE)
        {
            m_app.test_packet_counter++;
        }
    }
    else
    {
        uint32_t err_code = app_timer_stop(m_timer);
        APP_ERROR_CHECK(err_code);

        coap_test_result_request_send();
    }
}

/**@brief This function handles UDP test packet period timeout. */
static void timeout_handler(void * p_context)
{
    m_timer_task_pending = true;
}

/***************************************************************************************************
 * @section Buttons
 **************************************************************************************************/

static void bsp_event_handler(bsp_event_t event)
{
    switch (event)
    {
        case BSP_EVENT_KEY_3:
            coap_addr_request_send();
            break;

        default:
            break;
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
    err_code = bsp_init(BSP_INIT_LED | BSP_INIT_BUTTONS, bsp_event_handler);
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
    otError error = otCoapStart(thread_ot_instance_get(), OT_DEFAULT_COAP_PORT);
    ASSERT(error == OT_ERROR_NONE);
}


/**@brief Function for initializing the test.
 */
static void test_init(void)
{
    udp_port_create();

    uint32_t err_code = app_timer_create(&m_timer, APP_TIMER_MODE_REPEATED, timeout_handler);
    APP_ERROR_CHECK(err_code);
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
        timeout_process();

        if (NRF_LOG_PROCESS() == false)
        {
            thread_sleep();
        }
    }
}

/**
 *@}
 **/
