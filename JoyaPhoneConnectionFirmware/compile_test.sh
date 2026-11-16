#!/bin/bash

west build --sysbuild -p always \
  -b nrf52dk/nrf52832 \
  -d build_dk . -- \
  '-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="/workspace/keys/joya_dev-private.pem"' \
  '-DDTC_OVERLAY_FILE=nrf52dk_nrf52832.overlay;nrf52dk_battery_adc.overlay' \
  -DCONFIG_JOYA_HAPTICS_TEST_INDICATOR=y