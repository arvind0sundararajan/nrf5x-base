#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "led.h"
#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_delay.h"
#include "app_timer.h"
#include "nrf_log_ctrl.h"
#include "nrf_log.h"
#include "nrf_log_default_backends.h"

#include "thread_utils.h"

#include <openthread/openthread.h>
#include <openthread/coap.h>
#include <openthread/dns.h>
#include <openthread/ip6.h>
#include <openthread/platform/alarm-milli.h>

#define LED NRF_GPIO_PIN_MAP(0,4)
#define RX_PIN_NUMBER 26
#define TX_PIN_NUMBER 27

#define DNS_SERVER_IP "2001:4860:4860::8888"

#define CLOUD_HOSTNAME            "coap.thethings.io"            /**< Hostname of the thethings.io cloud. */
#define CLOUD_URI_PATH            "v2/things/{THING-TOKEN}"      /**< Put your things URI here. */
#define CLOUD_THING_RESOURCE      "temp"                         /**< Thing resource name. */
#define CLOUD_COAP_CONTENT_FORMAT 50                             /**< Use application/json content format type. */

#define MESSAGE_SEND_RATE APP_TIMER_TICKS(5000)
APP_TIMER_DEF(message_send_timer);

typedef struct
{
    const char * p_cloud_hostname;
    const char * p_cloud_uri_path;
    const char * p_cloud_thing_resource;
    uint8_t      cloud_coap_content_format;
} thread_coap_server_information_t;

static thread_coap_server_information_t m_cloud_information =
{
    .p_cloud_hostname            = CLOUD_HOSTNAME,
    .p_cloud_uri_path            = CLOUD_URI_PATH,
    .p_cloud_thing_resource      = CLOUD_THING_RESOURCE,
    .cloud_coap_content_format   = CLOUD_COAP_CONTENT_FORMAT,
};

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

void thread_coap_json_send(const thread_coap_server_information_t * p_thread_coap_server_information,
                           uint16_t data);
otError thread_dns_utils_hostname_resolve(otInstance         * p_instance,
                                          const char         * p_hostname,
                                          otDnsResponseHandler p_dns_response_handler,
                                          void               * p_context);

// Callbacks
static void dns_response_handler(void         * p_context,
                                 const char   * p_hostname,
                                 otIp6Address * p_resolved_address,
                                 uint32_t       ttl,
                                 otError        error)
{
    if (error != OT_ERROR_NONE)
    {
        NRF_LOG_INFO("DNS response error %d.\r\n", error);
        return;
    }

    peer_address = *p_resolved_address;
}

static void thread_coap_handler_default(void                * p_context,
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

    NRF_LOG_INFO("State changed! Flags: 0x%08lx Current role: %d\r\n",
                 flags,
                 otThreadGetDeviceRole(p_context));
}

static void message_send_timer_callback(void* p_context) {
    NRF_LOG_INFO("Sending coap message!\n");
    //// If IPv6 address of the cloud is unspecified try to resolve hostname.
    //if (otIp6IsAddressEqual(&peer_address, &m_unspecified_ipv6))
    //{
    //    UNUSED_VARIABLE(thread_dns_utils_hostname_resolve(thread_ot_instance_get(),
    //                                                      m_cloud_information.p_cloud_hostname,
    //                                                      dns_response_handler,
    //                                                      NULL));
    //    return;
    //}
    //uint16_t dummy_data = 22;
    //thread_coap_json_send(&m_cloud_information, dummy_data);
}

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing the Application Timer Module
 */
static void timer_init(void)
{
    uint32_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_create(&message_send_timer, APP_TIMER_MODE_REPEATED, message_send_timer_callback);
    APP_ERROR_CHECK(err_code);
    //err_code = app_timer_start(message_send_timer, MESSAGE_SEND_RATE, NULL);
    //APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Constrained Application Protocol Module
 */
static void thread_coap_init(void)
{
    otError error = otCoapStart(thread_ot_instance_get(), OT_DEFAULT_COAP_PORT);
    ASSERT(error == OT_ERROR_NONE);

    otCoapSetDefaultHandler(thread_ot_instance_get(), thread_coap_handler_default, NULL);
}

/**@brief Function for initializing the Thread Stack
 */
static void thread_instance_init(void)
{
    thread_init(&thread_configuration);
    thread_state_changed_callback_set(thread_state_changed_callback);
}

otError thread_dns_utils_hostname_resolve(otInstance         * p_instance,
                                          const char         * p_hostname,
                                          otDnsResponseHandler p_dns_response_handler,
                                          void               * p_context)
{
    otError       error;
    otMessageInfo message_info;

    memset(&message_info, 0, sizeof(message_info));
    message_info.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
    message_info.mPeerPort    = OT_DNS_DEFAULT_DNS_SERVER_PORT;
    error = otIp6AddressFromString(DNS_SERVER_IP, &message_info.mPeerAddr);

    if (error == OT_ERROR_NONE)
    {
        otDnsQuery query;

        query.mHostname    = p_hostname;
        query.mMessageInfo = &message_info;
        query.mNoRecursion = false;

        error = otDnsClientQuery(p_instance, &query, p_dns_response_handler, p_context);
    }

    if (error != OT_ERROR_NONE)
    {
        NRF_LOG_INFO("Failed to perform DNS Query.\r\n");
    }

    return error;
}

static void thread_coap_data_send(otInstance* p_instance,
                            const thread_coap_server_information_t * p_thread_coap_server_information,
                            char                                  * p_payload)
{
    otError       error;
    otCoapHeader  header;
    otCoapOption  content_format_option;
    otMessage   * p_request;
    otMessageInfo message_info;
    uint8_t content_format = p_thread_coap_server_information->cloud_coap_content_format;
    do
    {
        content_format_option.mNumber = OT_COAP_OPTION_CONTENT_FORMAT;
        content_format_option.mLength = 1;
        content_format_option.mValue  = &content_format;

        otCoapHeaderInit(&header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
        UNUSED_VARIABLE(otCoapHeaderAppendUriPathOptions(&header, p_thread_coap_server_information->p_cloud_uri_path));
        UNUSED_VARIABLE(otCoapHeaderAppendOption(&header, &content_format_option));
        otCoapHeaderSetPayloadMarker(&header);

        p_request = otCoapNewMessage(p_instance, &header);
        if (p_request == NULL)
        {
            NRF_LOG_INFO("Failed to allocate message for CoAP Request\r\n");
            break;
        }

        error = otMessageAppend(p_request, p_payload, strlen(p_payload));
        if (error != OT_ERROR_NONE)
        {
            break;
        }

        memset(&message_info, 0, sizeof(message_info));
        message_info.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
        message_info.mPeerPort = OT_DEFAULT_COAP_PORT;
        message_info.mPeerAddr = peer_address;

        error = otCoapSendRequest(p_instance, p_request, &message_info, NULL, NULL);

    } while (false);

    if (error != OT_ERROR_NONE && p_request != NULL)
    {
        NRF_LOG_INFO("Failed to send CoAP Request: %d\r\n", error);
        otMessageFree(p_request);
    }
}

void thread_coap_json_send(const thread_coap_server_information_t * p_thread_coap_server_information,
                           uint16_t data)
{
    char payload_buffer[64];

    sprintf(payload_buffer,
            "{\"values\":[{\"key\":\"%s\",\"value\":\"%d\"}]}",
            p_thread_coap_server_information->p_cloud_thing_resource, data);

    thread_coap_data_send(thread_ot_instance_get(), p_thread_coap_server_information, payload_buffer);
}

int main(void) {
    // Initialize.
    nrf_sdh_enable_request();
    led_init(LED);
    NRF_LOG_INFO("LED init done!");
    led_on(LED);

    log_init();
    NRF_LOG_INFO("LOG init done!");
    timer_init();
    NRF_LOG_INFO("TIMER init done!");

    thread_instance_init();
    NRF_LOG_INFO("THREAD init done!");
    thread_coap_init();

    NRF_LOG_INFO("Init done!");

    while (true)
    {
        thread_process();

        if (NRF_LOG_PROCESS() == false)
        {
            thread_sleep();
        }
    }
}
