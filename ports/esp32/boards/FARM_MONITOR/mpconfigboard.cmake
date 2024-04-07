set(IDF_TARGET esp32c3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    ${SDKCONFIG_IDF_VERSION_SPECIFIC}
    boards/sdkconfig.ble
    boards/ESP32_GENERIC_C3/sdkconfig.c3usb
)


list(APPEND MICROPY_SOURCE_PORT
    boards/FARM_MONITOR/gsm0710/buffer.c
    boards/FARM_MONITOR/gsm0710/gsm0710.c
    boards/FARM_MONITOR/gsm0710/mpy.c
)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
