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

#include <openthread/openthread.h>
#include <openthread/thread.h>
#include <openthread/coap.h>
#include <openthread/dns.h>
#include <openthread/ip6.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/platform.h>
#include <openthread/cli.h>
#include <openthread/diag.h>

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

static otInstance * mp_ot_instance;

static const otMasterKey p_thread_master_key = {
    .m8 = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
           0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}
};

/**@brief Enumeration describing thread roles.
 *
 * @details RX_ON_WHEN_IDLE causes the device to keep its receiver on when it is idle.
 *          Selecting RX_OFF_WHEN_IDLE results in creating the Sleepy End Device.
 */
typedef enum
{
    RX_ON_WHEN_IDLE = 0,  /**< Powered device */
    RX_OFF_WHEN_IDLE,     /**< Sleepy End Device */
} thread_role_t;

/**@brief Structure holding Thread configuration parameters.
 */
typedef struct
{
    thread_role_t role;                    /**< Selected Thread role. */
    bool          autostart_disable;       /**< If node should not start the Thread operation automatically. */
    bool          autocommissioning;       /**< If node should be commissioned automatically. */
    uint32_t      poll_period;             /**< Default SED poll period in miliseconds. */
    uint32_t      default_child_timeout;   /**< Thread child timeout value in seconds. */
} thread_configuration_t;

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

/**@brief Type definition of the function used to handle Thread Network state change taking taking uint32_t flags and void context pointer and returning void.
 *
 * @param[in] flags     Bit-field indicating state that has changed.
 * @param[in] p_context Context pointer to be used by the callback function.
 */
typedef void (*thread_state_change_callback_t)(uint32_t flags, void * p_context);

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

void thread_init(const thread_configuration_t * p_thread_configuration);
void thread_state_changed_callback_set(thread_state_change_callback_t handler);

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

    NRF_LOG_INFO("State changed! Flags: Current role: %d\r\n",
                 //flags,
                 otThreadGetDeviceRole(p_context));
}

static void message_send_timer_callback(void* p_context) {
    switch(otThreadGetDeviceRole(mp_ot_instance)) {
      case OT_DEVICE_ROLE_DISABLED:
        NRF_LOG_INFO("Device state: disabled");
        break;
      case OT_DEVICE_ROLE_DETACHED:
        NRF_LOG_INFO("Device state: detatched");
        break;
      case OT_DEVICE_ROLE_CHILD:
        NRF_LOG_INFO("Device state: child");
        break;
      default:
        break;
    }
    //// If IPv6 address of the cloud is unspecified try to resolve hostname.
    //if (otIp6IsAddressEqual(&peer_address, &m_unspecified_ipv6))
    //{
    //    UNUSED_VARIABLE(thread_dns_utils_hostname_resolve(&mp_ot_instance,
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
    err_code = app_timer_start(message_send_timer, MESSAGE_SEND_RATE, NULL);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Constrained Application Protocol Module
 */
static void thread_coap_init(void)
{
    otError error = otCoapStart(mp_ot_instance, OT_DEFAULT_COAP_PORT);
    ASSERT(error == OT_ERROR_NONE);

    otCoapSetDefaultHandler(mp_ot_instance, thread_coap_handler_default, NULL);
}

/**@brief Function for initializing the Thread Stack
 */
static void thread_instance_init(void)
{
    thread_init(&thread_configuration);
    thread_state_changed_callback_set(thread_state_changed_callback);
}

/**@brief Function for setting state changed callback
 */
void thread_state_changed_callback_set(thread_state_change_callback_t handler)
{
    ASSERT(mp_ot_instance != NULL);

    otError error = otSetStateChangedCallback(mp_ot_instance, handler, mp_ot_instance);
    ASSERT(error == OT_ERROR_NONE);
}

/**@brief Function for initializing the Thread Stack
 */
void thread_init(const thread_configuration_t * p_thread_configuration)
{
    otError error;

    PlatformInit(0, NULL);

    mp_ot_instance = otInstanceInitSingle();
    ASSERT(mp_ot_instance != NULL);

    NRF_LOG_INFO("Thread version: %s\r\n", (uint32_t)otGetVersionString());

    if (!otDatasetIsCommissioned(mp_ot_instance) p_thread_configuration->autocommissioning)
    {
        error = otThreadSetNetworkName(mp_ot_instance, "OpenThread");
        ASSERT(error == OT_ERROR_NONE);
        error = otLinkSetChannel(mp_ot_instance, THREAD_CHANNEL);
        ASSERT(error == OT_ERROR_NONE);

        error = otLinkSetPanId(mp_ot_instance, THREAD_PANID);
        ASSERT(error == OT_ERROR_NONE);
        error = otThreadSetMasterKey(mp_ot_instance, &p_thread_master_key);
        ASSERT(error == OT_ERROR_NONE);
    }

    if (p_thread_configuration->role == RX_OFF_WHEN_IDLE)
    {
        otLinkModeConfig mode;
        memset(&mode, 0, sizeof(mode));
        mode.mRxOnWhenIdle       = false; // Join network as SED.
        mode.mSecureDataRequests = true;
        error = otThreadSetLinkMode(mp_ot_instance, mode);
        ASSERT(error == OT_ERROR_NONE);

        otLinkSetPollPeriod(mp_ot_instance, p_thread_configuration->poll_period);
    }

    if (p_thread_configuration->default_child_timeout != 0)
    {
        otThreadSetChildTimeout(mp_ot_instance, p_thread_configuration->default_child_timeout);
    }

    if (!p_thread_configuration->autostart_disable)
    {
        error = otIp6SetEnabled(mp_ot_instance, true);
        ASSERT(error == OT_ERROR_NONE);

        if (otDatasetIsCommissioned(mp_ot_instance) || p_thread_configuration->autocommissioning)
        {
            error = otThreadSetEnabled(mp_ot_instance, true);
            ASSERT(error == OT_ERROR_NONE);

            NRF_LOG_INFO("Thread interface has been enabled.\r\n");
            NRF_LOG_INFO("Network name:   %s\r\n",
                 (uint32_t)otThreadGetNetworkName(mp_ot_instance));
            NRF_LOG_INFO("802.15.4 Channel: %d\r\n", otLinkGetChannel(mp_ot_instance));
            NRF_LOG_INFO("802.15.4 PAN ID:  0x%04x\r\n", otLinkGetPanId(mp_ot_instance));
            const otMasterKey* current_master_key = otThreadGetMasterKey(mp_ot_instance);
            NRF_LOG_INFO("802.15.4 Master Key:  0x%02x%02x%02x%02x\r\n", current_master_key->m8[0], current_master_key->m8[1], current_master_key->m8[2], current_master_key->m8[3]);
            otLinkModeConfig link_mode = otThreadGetLinkMode(mp_ot_instance);
            NRF_LOG_INFO("rx-on-when-idle:  %s secure-data: %s device-type: %s network-data: %s\r\n",
              link_mode.mRxOnWhenIdle ? "enabled" : "disabled",
              link_mode.mSecureDataRequests ? "y" : "n",
              link_mode.mDeviceType ? "y" : "n",
              link_mode.mNetworkData ? "y" : "n");
        }
    }
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

    thread_coap_data_send(mp_ot_instance, p_thread_coap_server_information, payload_buffer);
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
        otTaskletsProcess(mp_ot_instance);
        PlatformProcessDrivers(mp_ot_instance);

        if (NRF_LOG_PROCESS() == false)
        {
            //// Enter sleep state if no more tasks are pending.
            //if (!otTaskletsArePending(mp_ot_instance))
            //{
            //  ret_code_t err_code = sd_app_evt_wait();
            //  ASSERT(err_code == NRF_SUCCESS);
            //}
        }
    }
}
