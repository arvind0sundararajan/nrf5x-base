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

#include "mqttsn_client.h"
#include "mqttsn_packet_internal.h"

#include <stdint.h>
#include <stdbool.h>

/**@brief Next timer timeout value. */
static uint32_t m_next_timeout = UINT32_MAX;

#define NULL_PARAM_CHECK(PARAM)                                                                    \
    if ((PARAM) == NULL)                                                                           \
    {                                                                                              \
        return (NRF_ERROR_NULL);                                                                   \
    }

/**@brief Checks if timer has already passed a scheduled event timeout.  
 *
 * @param[in]    timeout     Scheduled event timeout.
 * @param[in]    now         Current timer value.
 *
 * @retval       true        If the scheduled event timeout has passed.
 * @retval       false       Otherwise.
 */
static inline bool is_earlier(uint32_t timeout, uint32_t now)
{
    return ((timeout - now) & mqttsn_platform_timer_resolution_get()) != 0;
}

/**@brief Checks if MQTT-SN client has been initialized. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 *
 * @retval       true        If the client is initialized.
 * @retval       false       Otherwise.
 */
static inline bool is_initialized(mqttsn_client_t * p_client)
{
    return p_client->client_state != MQTTSN_CLIENT_IDLE;
}

/**@brief Checks if MQTT-SN client is connected to the network. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 *
 * @retval       true        If the client is connected.
 * @retval       false       Otherwise.
 */
static inline bool is_connected(mqttsn_client_t * p_client)
{
    return p_client->client_state == MQTTSN_CLIENT_CONNECTED;    
}

/**@brief Checks if MQTT-SN client is in asleep state. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 *
 * @retval       true        If the client is asleep.
 * @retval       false       Otherwise.
 */
static inline bool is_asleep(mqttsn_client_t * p_client)
{
    return p_client->client_state == MQTTSN_CLIENT_ASLEEP;
}

/**@brief Checks if MQTT-SN client is disconnected.
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 *
 * @retval       true        If the client is disconnected.
 * @retval       false       Otherwise.
 */
static inline bool is_disconnected(mqttsn_client_t * p_client)
{
    return p_client->client_state == MQTTSN_CLIENT_DISCONNECTED;
}

/**@brief Checks if MQTT-SN client can try to connect to the broker. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 *
 * @retval       true        If the client is eligible for establishing connection.
 * @retval       false       Otherwise.
 */
static inline bool is_eligible_for_establishing_connection(mqttsn_client_t * p_client)
{
    return ( p_client->client_state == MQTTSN_CLIENT_GATEWAY_FOUND ||
             p_client->client_state == MQTTSN_CLIENT_ASLEEP);
}

/**@brief Initializes MQTT-SN client's connect options. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 * @param[in]    p_options   Pointer to connect options.
 */
static void connect_info_init(mqttsn_client_t * p_client, mqttsn_connect_opt_t * p_options)
{
    p_client->connect_info.alive_duration = p_options->alive_duration;
    p_client->connect_info.clean_session  = p_options->clean_session;
    p_client->connect_info.will_flag      = p_options->will_flag;

    if (p_options->will_flag)
    {
        p_client->connect_info.will_topic_len = p_options->will_topic_len;
        memset(p_client->connect_info.p_will_topic, 0, p_options->will_topic_len);
        memcpy(p_client->connect_info.p_will_topic, p_options->p_will_topic, p_options->will_topic_len);

        p_client->connect_info.will_msg_len = p_options->will_msg_len;
        memset(p_client->connect_info.p_will_msg, 0, p_options->will_msg_len);
        memcpy(p_client->connect_info.p_will_msg, p_options->p_will_msg, p_options->will_msg_len);
    }

    p_client->connect_info.client_id_len = p_options->client_id_len;
    memset(p_client->connect_info.p_client_id, 0, p_options->client_id_len);
    memcpy(p_client->connect_info.p_client_id, p_options->p_client_id, p_options->client_id_len);
}

/**@brief Attempts retransmission if retransmission limit has not been reached; otherwise throws event timeout. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 * @param[in]    index       Index of the message from client's packet queue to retransmit.
 */
static void message_retransmission_attempt(mqttsn_client_t * p_client, uint8_t index)
{
    if (p_client->packet_queue.packet[index].retransmission_cnt == 0)
    {
        mqttsn_event_t evt;
        memset(&evt, 0, sizeof(mqttsn_event_t)); 
        evt.event_id                  = MQTTSN_EVENT_TIMEOUT;
        evt.event_data.error.error    = MQTTSN_ERROR_TIMEOUT;
        evt.event_data.error.msg_type =
            mqttsn_packet_msgtype_error_get(p_client->packet_queue.packet[index].p_data);
        evt.event_data.error.msg_id   = p_client->packet_queue.packet[index].id;

        uint32_t err_code = NRF_SUCCESS;

        switch (mqttsn_packet_msgtype_error_get(p_client->packet_queue.packet[index].p_data))
        {
            case MQTTSN_PACKET_CONNACK:
                err_code = mqttsn_packet_fifo_elem_dequeue(p_client,
                                                           MQTTSN_MSGTYPE_CONNECT, 
                                                           MQTTSN_MESSAGE_TYPE);
                ASSERT(err_code == NRF_SUCCESS);
                break;

            case MQTTSN_PACKET_WILLTOPICUPD:
                err_code = mqttsn_packet_fifo_elem_dequeue(p_client, 
                                                           MQTTSN_MSGTYPE_WILLTOPICUPD,
                                                           MQTTSN_MESSAGE_TYPE);
                ASSERT(err_code == NRF_SUCCESS);
                break;

            case MQTTSN_PACKET_WILLMSGUPD:
                err_code = mqttsn_packet_fifo_elem_dequeue(p_client,
                                                           MQTTSN_MSGTYPE_WILLMSGUPD,
                                                           MQTTSN_MESSAGE_TYPE);
                ASSERT(err_code == NRF_SUCCESS);
                break;

            default:
                err_code = mqttsn_packet_fifo_elem_dequeue(p_client,
                                                           p_client->packet_queue.packet[index].id,
                                                           MQTTSN_MESSAGE_ID);
                ASSERT(err_code == NRF_SUCCESS);
                break;
        }

        p_client->evt_handler(p_client, &evt);
        return;
    }
    else
    {
        --(p_client->packet_queue.packet[index].retransmission_cnt);
        p_client->packet_queue.packet[index].timeout =
            mqttsn_platform_timer_set_in_ms(MQTTSN_DEFAULT_RETRANSMISSION_TIME_IN_MS);
        uint32_t err_code = mqttsn_packet_sender_retransmit(p_client,
                                                            &(p_client->gateway_info.addr),
                                                            p_client->packet_queue.packet[index].p_data,
                                                            p_client->packet_queue.packet[index].len);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("Retransmission attempt failed. Error code: 0x%x\r\n", err_code);
        }
    }
}

/**@brief Attempts keep-alive transmission if retransmission limit has not been reached; otherwise throws event timeout. 
 *
 * @param[in]    p_client    Pointer to MQTT-SN client instance.
 */
static void keep_alive_transmission_attempt(mqttsn_client_t * p_client)
{
    /* Retransmission limit reached : throw retransmission timeout event to the application. */
    if (p_client->keep_alive.message.retransmission_cnt == 0)
    {
        mqttsn_event_t evt;
        memset(&evt, 0, sizeof(mqttsn_event_t));
        evt.event_id                  = MQTTSN_EVENT_TIMEOUT;
        evt.event_data.error.error    = MQTTSN_ERROR_TIMEOUT;
        evt.event_data.error.msg_type = MQTTSN_PACKET_PINGREQ;

        p_client->evt_handler(p_client, &evt);
    }
    /* Usual procedure : set timer for retransmission. Receiving response will postpone timeout. */
    else if (p_client->keep_alive.response_arrived == 0)
    {
        if (p_client->client_state == MQTTSN_CLIENT_ASLEEP)
        {
            mqttsn_event_t evt = { .event_id = MQTTSN_EVENT_SLEEP_STOP };
            p_client->evt_handler(p_client, &evt);
        }
        --(p_client->keep_alive.message.retransmission_cnt);
        p_client->keep_alive.timeout = mqttsn_platform_timer_set_in_ms(MQTTSN_DEFAULT_RETRANSMISSION_TIME_IN_MS);
        uint32_t err_code = mqttsn_packet_sender_retransmit(p_client,
                                                            &(p_client->gateway_info.addr),
                                                            p_client->keep_alive.message.p_data,
                                                            p_client->keep_alive.message.len);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("Retransmission attempt failed. Error code: 0x%x\r\n", err_code);
        }
    }
}

uint32_t mqttsn_client_init(mqttsn_client_t           * p_client,
                            uint16_t                    port,
                            mqttsn_client_evt_handler_t evt_handler,
                            const void                * p_transport_context)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(evt_handler);
    NULL_PARAM_CHECK(p_transport_context);

    uint32_t err_code = NRF_SUCCESS;
    mqttsn_packet_fifo_init(p_client);

    if (nrf_mem_init()!= NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Memory manager failed to initialize\r\n");
        return NRF_ERROR_INTERNAL;
    }

    if (mqttsn_transport_init(p_client, port, p_transport_context) != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Transport failed to initialize\r\n");
        return NRF_ERROR_INTERNAL;
    }

    if (mqttsn_platform_init() != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Platform failed to initialize\r\n");
        return NRF_ERROR_INTERNAL;
    }

    p_client->client_state = MQTTSN_CLIENT_DISCONNECTED;
    p_client->evt_handler  = evt_handler;

    return err_code;
}

uint32_t mqttsn_client_search_gateway(mqttsn_client_t * p_client)
{
    NULL_PARAM_CHECK(p_client);
    if (!is_initialized(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    if (!is_disconnected(p_client))
    {
        return NRF_ERROR_INVALID_STATE;
    }

    uint16_t rnd_jitter = mqttsn_platform_rand(MQTTSN_SEARCH_GATEWAY_MAX_DELAY_IN_MS);
    if (mqttsn_platform_timer_start(p_client, mqttsn_platform_timer_set_in_ms(rnd_jitter)) !=
        NRF_SUCCESS)
    {
        NRF_LOG_ERROR("MQTT-SN platform timer failed to start for SEARCH GATEWAY random jitter generation.\r\n");
        return NRF_ERROR_INTERNAL;
    }
    p_client->client_state = MQTTSN_CLIENT_SEARCHING_GATEWAY;

    return NRF_SUCCESS;
}

uint32_t mqttsn_client_connect(mqttsn_client_t      * p_client,
                               mqttsn_remote_t      * p_remote,
                               uint8_t                gateway_id,
                               mqttsn_connect_opt_t * p_options)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_remote);
    NULL_PARAM_CHECK(p_options);

    if (gateway_id == 0 || p_options->alive_duration == 0 || p_options->client_id_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (p_options->will_flag == 1 && (p_options->will_msg_len == 0 || p_options->will_topic_len == 0))
    {
        return NRF_ERROR_NULL;
    }
    
    if (!is_initialized(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    if (!is_eligible_for_establishing_connection(p_client))
    {
        return NRF_ERROR_INVALID_STATE;
    }

    memset(&(p_client->gateway_info.addr), 0, sizeof(p_client->gateway_info.addr));
    memcpy(&(p_client->gateway_info.addr), p_remote, sizeof(p_client->gateway_info.addr));

    p_client->gateway_info.id = gateway_id;

    connect_info_init(p_client, p_options);

    p_client->client_state = MQTTSN_CLIENT_ESTABLISHING_CONNECTION;

    return mqttsn_packet_sender_connect(p_client);
}

uint32_t mqttsn_client_disconnect(mqttsn_client_t * p_client)
{
    NULL_PARAM_CHECK(p_client);

    if (!is_connected(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    return mqttsn_packet_sender_disconnect(p_client, 0);
}

uint32_t mqttsn_client_sleep(mqttsn_client_t * p_client, uint16_t polling_time)
{
    NULL_PARAM_CHECK(p_client);

    if (!is_connected(p_client) && !is_asleep(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    return mqttsn_packet_sender_disconnect(p_client, polling_time);
}

uint32_t mqttsn_client_publish(mqttsn_client_t * p_client,
                               uint16_t          topic_id,
                               const uint8_t   * p_payload,
                               uint16_t          payload_len,
                               uint16_t        * p_msg_id)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_payload);

    if(topic_id == 0 || payload_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (!is_connected(p_client) && !is_asleep(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    mqttsn_topic_t topic = { .topic_id = topic_id };

    uint32_t err_code = mqttsn_packet_sender_publish(p_client, &topic, p_payload, payload_len);
    if (p_msg_id)
    {
        *p_msg_id = p_client->message_id;
    }

    return err_code;
}

uint32_t mqttsn_client_topic_register(mqttsn_client_t * p_client,
                                      const uint8_t   * p_topic_name,
                                      uint16_t          topic_name_len,
                                      uint16_t        * p_msg_id)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_topic_name)
    if (topic_name_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (!is_connected(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    mqttsn_topic_t topic = { .p_topic_name = p_topic_name };
    
    uint32_t err_code = mqttsn_packet_sender_register(p_client, &topic, topic_name_len);
    if (p_msg_id)
    {
        *p_msg_id = p_client->message_id;
    }

    return err_code;
}

uint32_t mqttsn_client_subscribe(mqttsn_client_t * p_client,
                                 const uint8_t   * p_topic_name,
                                 uint16_t          topic_name_len,
                                 uint16_t        * p_msg_id)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_topic_name);
    if (topic_name_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (!is_connected(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    mqttsn_topic_t topic = { .p_topic_name = p_topic_name };

    uint32_t err_code = mqttsn_packet_sender_subscribe(p_client, &topic, topic_name_len);
    
    if (p_msg_id)
    {
        *p_msg_id = p_client->message_id;
    }

    return err_code;
}

uint32_t mqttsn_client_unsubscribe(mqttsn_client_t * p_client,
                                   const uint8_t   * p_topic_name,
                                   uint16_t          topic_name_len,
                                   uint16_t        * p_msg_id)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_topic_name);
    if (topic_name_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (!is_connected(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    mqttsn_topic_t topic = { .p_topic_name = p_topic_name };

    uint32_t err_code = mqttsn_packet_sender_unsubscribe(p_client, &topic, topic_name_len);
    if (p_msg_id)
    {
        *p_msg_id = p_client->message_id;
    }

    return err_code;
}

uint32_t mqttsn_client_willtopicupd(mqttsn_client_t * p_client,
                                    const uint8_t   * p_will_topic,
                                    uint16_t          will_topic_len)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_will_topic);
    NULL_PARAM_CHECK(p_client->connect_info.p_will_topic);

    if (will_topic_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (!is_connected(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    p_client->connect_info.will_topic_len = will_topic_len;
    memset(p_client->connect_info.p_will_topic, 0, MQTTSN_WILL_TOPIC_MAX_LENGTH);
    memcpy(p_client->connect_info.p_will_topic, p_will_topic, will_topic_len);

    return mqttsn_packet_sender_willtopicupd(p_client);
}

uint32_t mqttsn_client_willmsgupd(mqttsn_client_t * p_client,
                                  const uint8_t   * p_will_msg,
                                  uint16_t          will_msg_len)
{
    NULL_PARAM_CHECK(p_client);
    NULL_PARAM_CHECK(p_will_msg);
    NULL_PARAM_CHECK(p_client->connect_info.p_will_msg);

    if (will_msg_len == 0)
    {
        return NRF_ERROR_NULL;
    }

    if (!is_connected(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    p_client->connect_info.will_msg_len = will_msg_len;
    memset(p_client->connect_info.p_will_msg, 0, MQTTSN_WILL_MSG_MAX_LENGTH);
    memcpy(p_client->connect_info.p_will_msg, p_will_msg, will_msg_len);

    return mqttsn_packet_sender_willmsgupd(p_client);
}

uint32_t mqttsn_client_uninit(mqttsn_client_t * p_client)
{
    NULL_PARAM_CHECK(p_client);

    if (!is_initialized(p_client))
    {
        return NRF_ERROR_FORBIDDEN;
    }

    mqttsn_packet_fifo_uninit(p_client);
    return mqttsn_transport_uninit(p_client) == 0 ? NRF_SUCCESS : NRF_ERROR_INTERNAL;
}

void mqttsn_client_timer_timeout_handle(mqttsn_client_t * p_client)
{
    uint32_t next_timeout = UINT32_MAX;
    uint32_t timer_value  = mqttsn_platform_timer_cnt_get();

    /* Random jitter for SEARCH GATEWAY message. */
    if (p_client->client_state == MQTTSN_CLIENT_SEARCHING_GATEWAY)
    {
        uint32_t err_code = mqttsn_packet_sender_searchgw(p_client);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("SEARCH GATEWAY message could not be sent. Error code: 0x%x\r\n", err_code);
        }
        return;
    }

    /* Retransmission handler. */
    for (int i = 0; i < p_client->packet_queue.num_of_elements; i++)
    {
        if (is_earlier(p_client->packet_queue.packet[i].timeout, m_next_timeout))
        {
            message_retransmission_attempt(p_client, i);
        }
        
        if (p_client->packet_queue.packet[i].timeout - timer_value < next_timeout)
        {
            next_timeout = p_client->packet_queue.packet[i].timeout - timer_value;
        }
    }

    /* Keep alive (pingreq) message. */
    if (m_next_timeout != UINT32_MAX)
    {
        if (is_earlier(p_client->keep_alive.timeout, m_next_timeout))
        {
            keep_alive_transmission_attempt(p_client);
        }
    }

    if (p_client->keep_alive.timeout - timer_value < next_timeout)
    {
        next_timeout = p_client->keep_alive.timeout - timer_value;
        next_timeout = next_timeout % mqttsn_platform_timer_resolution_get();
        p_client->keep_alive.response_arrived = 0;
    }

    m_next_timeout = next_timeout;

    if (mqttsn_platform_timer_start(p_client, m_next_timeout) != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("MQTT-SN platform timer failed to start for the next timeout.\r\n");
    }
}
