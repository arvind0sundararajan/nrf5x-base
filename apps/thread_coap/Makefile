PROJECT_NAME = $(shell basename "$(realpath ./)")

USE_THREAD = 1

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += app_error.c
APPLICATION_SRCS += app_error_weak.c
APPLICATION_SRCS += nrf_drv_common.c
APPLICATION_SRCS += nrf_drv_clock.c
APPLICATION_SRCS += led.c
APPLICATION_SRCS += thread_utils.c
APPLICATION_SRCS += nrf_log_backend_serial.c
APPLICATION_SRCS += SEGGER_RTT.c
APPLICATION_SRCS += SEGGER_RTT_Syscalls_GCC.c
APPLICATION_SRCS += SEGGER_RTT_printf.c
APPLICATION_SRCS += nrf_log_backend_rtt.c

NRF_MODEL = nrf52840

SDK_VERSION = 14
SOFTDEVICE_MODEL = s140

NRF_BASE_PATH ?= ../..

LIBRARY_PATHS += .  ../../include
SOURCE_PATHS += ../../src

LINKER_SCRIPT = $(OPENTHREAD_DIR)examples/platforms/nrf52840/nrf52840.ld

include $(NRF_BASE_PATH)/make/Makefile
