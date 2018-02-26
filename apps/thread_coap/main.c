/* Blink
 */

#include <stdbool.h>
#include <stdint.h>
#include "led.h"
#include "nrf.h"
#include "nrf_delay.h"

//#include "thread_coap_utils.h"
#include "thread_utils.h"

#define LED NRF_GPIO_PIN_MAP(0,13)

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
                //thread_coap_utils_peer_addr_clear();
                break;
        }
    }

    if (flags & OT_CHANGED_THREAD_PARTITION_ID)
    {
        //thread_coap_utils_peer_addr_clear();
    }

    //NRF_LOG_INFO("State changed! Flags: 0x%08x Current role: %d\r\n",
    //             flags,
    //             otThreadGetDeviceRole(p_context));
}

/**@brief Function for initializing the Thread Stack
 */
static void thread_instance_init(void)
{
    thread_configuration_t thread_configuration =
    {
        .role              = RX_OFF_WHEN_IDLE,
        .autocommissioning = true,
    };

    thread_init(&thread_configuration);
    thread_state_changed_callback_set(thread_state_changed_callback);
}

int main(void) {

    thread_instance_init();
    //thread_coap_init();

    // Initialize.
    led_init(LED);
    led_off(LED);

    // Enter main loop.
    while (1) {
      nrf_delay_ms(500);
      led_toggle(LED);
    }
}
