PROJECT_NAME = $(shell basename "$(realpath ./)")

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += app_error.c
APPLICATION_SRCS += app_error_weak.c
APPLICATION_SRCS += app_util_platform.c

LIBRARY_PATHS += $(SDK_PATH)external/segger_rtt/
SOURCE_PATHS  += $(SDK_PATH)external/segger_rtt/
LIBRARY_PATHS += $(SDK_PATH)external/fprintf/
SOURCE_PATHS += $(SDK_PATH)external/fprintf/
APPLICATION_SRCS += SEGGER_RTT.c
APPLICATION_SRCS += SEGGER_RTT_Syscalls_GCC.c
APPLICATION_SRCS += SEGGER_RTT_printf.c
APPLICATION_SRCS += nrf_memobj.c
APPLICATION_SRCS += nrf_balloc.c
APPLICATION_SRCS += nrf_log_backend_rtt.c
APPLICATION_SRCS += nrf_log_backend_serial.c
APPLICATION_SRCS += nrf_log_frontend.c
APPLICATION_SRCS += nrf_log_default_backends.c
APPLICATION_SRCS += nrf_log_str_formatter.c
APPLICATION_SRCS += nrf_fprintf.c
APPLICATION_SRCS += nrf_fprintf_format.c

NRF_MODEL = nrf52840

SDK_VERSION = 14
SOFTDEVICE_MODEL = s140

NRF_BASE_PATH ?= ../..

LIBRARY_PATHS += .  ../../include
SOURCE_PATHS += ../../src

#LINKER_SCRIPT = $(OPENTHREAD_DIR)examples/platforms/nrf52840/nrf52840.ld

include $(NRF_BASE_PATH)/make/Makefile
