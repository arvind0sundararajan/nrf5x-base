#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "led.h"
#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
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

#define DNS_SERVER_IP "2001:4860:4860::8888"

#define MESSAGE_SEND_RATE APP_TIMER_TICKS(5000)
APP_TIMER_DEF(message_send_timer);

static otInstance * mp_ot_instance;

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

void thread_init(const thread_configuration_t * p_thread_configuration);
static void thread_state_changed_callback(uint32_t flags, void * p_context);

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

/**@brief Function for initializing the Thread Stack
 */
static void thread_instance_init(void)
{
    thread_init(&thread_configuration);
    otError error = otSetStateChangedCallback(mp_ot_instance, thread_state_changed_callback, NULL);
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


    error = otLinkSetChannel(mp_ot_instance, THREAD_CHANNEL);
    ASSERT(error == OT_ERROR_NONE);

    error = otLinkSetPanId(mp_ot_instance, THREAD_PANID);
    ASSERT(error == OT_ERROR_NONE);

    //otLinkModeConfig mode;
    //memset(&mode, 0, sizeof(mode));
    //mode.mRxOnWhenIdle       = false; // Join network as SED.
    //mode.mSecureDataRequests = true;
    //error = otThreadSetLinkMode(mp_ot_instance, mode);
    //ASSERT(error == OT_ERROR_NONE);

    //otLinkSetPollPeriod(mp_ot_instance, p_thread_configuration->poll_period);

    //otThreadSetChildTimeout(mp_ot_instance, p_thread_configuration->default_child_timeout);

    error = otIp6SetEnabled(mp_ot_instance, true);
    ASSERT(error == OT_ERROR_NONE);

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

int main(void) {
    // Initialize.
    ret_code_t err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    led_init(LED);
    led_on(LED);

    log_init();
    timer_init();

    thread_instance_init();
    //otCliUartInit(mp_ot_instance);

    NRF_LOG_INFO("Init done!");

    while (true)
    {
        otTaskletsProcess(mp_ot_instance);
        PlatformProcessDrivers(mp_ot_instance);

        if (NRF_LOG_PROCESS() == false && !otTaskletsArePending(mp_ot_instance))
        {
            err_code = sd_app_evt_wait();
            APP_ERROR_CHECK(err_code);
        }
    }
}
