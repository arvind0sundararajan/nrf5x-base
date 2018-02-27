/* Blink
 */

#include <stdbool.h>
#include <stdint.h>
#include "led.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "app_timer.h"

#include "thread_utils.h"

#include <openthread/openthread.h>
#include <openthread/coap.h>
#include <openthread/ip6.h>
#include <openthread/platform/alarm-milli.h>

#define LED NRF_GPIO_PIN_MAP(0,13)

#define MESSAGE_SEND_RATE = APP_TIMER_TICKS(500)
APP_TIMER_DEF(message_send_timer);

thread_configuration_t thread_configuration =
{
  .role              = RX_OFF_WHEN_IDLE,
  .autocommissioning = true,
};


static const otIp6Address m_unspecified_ipv6 =
{
    .mFields =
    {
        .m8 = {0}
    }
};

static otIp6Address peer_address =
{
  .mFields =
  {
    .m8 = {0}
  }
};

typedef struct
{
    bool coap_server_enabled;
    bool coap_client_enabled;
    bool coap_cloud_enabled;
    bool configurable_led_blinking_enabled;
} thread_coap_configuration_t;

void thread_coap_unicast_request_send(otInstance* p_instance, char* command, size_t len);

static void thread_coap_utils_handler_default(void                * p_context,
                                              otCoapHeader        * p_header,
                                              otMessage           * p_message,
                                              const otMessageInfo * p_message_info)
{
    (void)p_context;
    (void)p_header;
    (void)p_message;
    (void)p_message_info;

    NRF_LOG_INFO("Received CoAP message that does not match any request or resource\r\n");
}

static void thread_state_changed_callback(uint32_t flags, void * p_context)
{
    if (flags & OT_CHANGED_THREAD_ROLE)
    {
        switch (otThreadGetDeviceRole(p_context))
        {
            case OT_DEVICE_ROLE_CHILD:
            case OT_DEVICE_ROLE_ROUTER:
            case OT_DEVICE_ROLE_LEADER:
                break;

            case OT_DEVICE_ROLE_DISABLED:
            case OT_DEVICE_ROLE_DETACHED:
            default:
                peer_address = m_unspecified_ipv6;
                break;
        }
    }

    if (flags & OT_CHANGED_THREAD_PARTITION_ID)
    {
        peer_address = m_unspecified_ipv6;
    }

    NRF_LOG_INFO("State changed! Flags: 0x%08x Current role: %d\r\n",
                 flags,
                 otThreadGetDeviceRole(p_context));
}

static void message_send_timer_callback(void* p_context) {
    char* to_send = "test";
    thread_coap_unicast_request_send(thread_ot_instance_get(), to_send, sizeof(to_send));
}

/**@brief Function for initializing the Application Timer Module
 */
static void timer_init(void)
{
    uint32_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_create(&message_send_timer, APP_TIMER_MODE_REPEATED, message_send_timer_callback);
}

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing the Constrained Application Protocol Module
 */
static void thread_coap_init(void)
{
    otError error = otCoapStart(thread_ot_instance_get(), OT_DEFAULT_COAP_PORT);
    ASSERT(error == OT_ERROR_NONE);

    otCoapSetDefaultHandler(thread_ot_instance_get(), thread_coap_utils_handler_default, NULL);
}

/**@brief Function for initializing the Thread Stack
 */
static void thread_instance_init(void)
{
    thread_init(&thread_configuration);
    thread_cli_init();
    thread_state_changed_callback_set(thread_state_changed_callback);
}

void thread_coap_unicast_request_send(otInstance* p_instance, char* command, size_t len)
{
    otError       error = OT_ERROR_NONE;
    otMessage   * p_message;
    otMessageInfo message_info;
    otCoapHeader  header;

    do
    {
        if (otIp6IsAddressEqual(&peer_address, &m_unspecified_ipv6))
        {
            NRF_LOG_INFO("Failed to send the CoAP Request to the Unspecified IPv6 Address\r\n");
            break;
        }

        otCoapHeaderInit(&header, OT_COAP_TYPE_CONFIRMABLE, OT_COAP_CODE_PUT);
        otCoapHeaderGenerateToken(&header, 2);
        UNUSED_VARIABLE(otCoapHeaderAppendUriPathOptions(&header, "light"));
        otCoapHeaderSetPayloadMarker(&header);

        p_message = otCoapNewMessage(p_instance, &header);
        if (p_message == NULL)
        {
            NRF_LOG_INFO("Failed to allocate message for CoAP Request\r\n");
            break;
        }

        error = otMessageAppend(p_message, &command, len);
        if (error != OT_ERROR_NONE)
        {
            break;
        }

        memset(&message_info, 0, sizeof(message_info));
        message_info.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
        message_info.mPeerPort    = OT_DEFAULT_COAP_PORT;
        memcpy(&message_info.mPeerAddr, &peer_address, sizeof(message_info.mPeerAddr));

        error = otCoapSendRequest(p_instance,
                                  p_message,
                                  &message_info,
                                  NULL,
                                  p_instance);
    } while (false);

    if (error != OT_ERROR_NONE && p_message != NULL)
    {
        NRF_LOG_INFO("Failed to send CoAP Request: %d\r\n", error);
        otMessageFree(p_message);
    }
}

int main(void) {

    log_init();
    timer_init();

    thread_instance_init();
    thread_coap_init();
    thread_instance_init();

    // Initialize.
    led_init(LED);
    led_off(LED);

    while (true)
    {
        thread_process();

        if (NRF_LOG_PROCESS() == false)
        {
            thread_sleep();
        }
    }
}
