#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_delay.h"
#include "nrf_log_ctrl.h"
#include "nrf_log.h"
#include "nrf_log_default_backends.h"

unsigned int counter = 0;

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

int main(void) {
    // Initialize.

    log_init();

    while (true)
    {
        nrf_delay_ms(500);
        NRF_LOG_INFO("printing log message #%u", counter++);
        NRF_LOG_PROCESS();
    }
}
