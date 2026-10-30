#!/bin/bash
west build -d build_joya -b joya_nrf52/nrf52832 . --sysbuild -- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/workspace/keys/joya_dev-private.pem\"