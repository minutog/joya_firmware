1. Se crea `sysbuild.conf` en el directorio raiz con `SB_CONFIG_BOOTLOADER_MCUBOOT=y`, para indicar que se deben construir dos imágenes coordinadas (MCUboot + aplicación)

2. Al compilar con `--sysbuild` se confirma que se generan:
    - build/JoyaPhoneConnectionFirmware/zephyr/zephyr.hex (aplicación)
    - build/JoyaPhoneConnectionFirmware/zephyr/zephyr.signed.bin (aplicación en formato binario con cabecera de MCUboot, información de tamaño y versión, hash y firma digital)
    - build/mcuboot/zephyr/zephyr.hex (firmware del bootloader MCUboot)
    - build/domains.yaml

3. Al revisar el mapa de la flash (`grep -A8 -B2 -E 'mcuboot_partition|slot0_partition|slot1_partition|storage_partition' build/JoyaPhoneConnectionFirmware/zephyr/zephyr.dts`) se observa:
    i. Mapa de particiones
        - MCUBoot desde `0x00000` a `0x0BFFF` (4096 bytes)
        - Slot0: la aplicación quedó configurada para ejecutar desde `slot0_partition: 0x0C000, tamaño 0x37000 (225280 bytes)`
        - Slot1: el segundo firmware se almacena en `slot1_partition: 0x43000, tamaño 0x37000 (225280 bytes)`
        - Almacenamiento persistente: las configuraciones y otros datos no volátiles se almacenan en `storage_partition: 0x7A000, tamaño 0x6000 (24576 bytes)`
    ii. `zephyr,code-partition = &slot0_partition;` confirma que la aplicación está enlazada para vivir en el slot activo y no al principio de la flash, donde ahora está MCUboot

4. Los ROM reports (`west build -d build/JoyaPhoneConnectionFirmware -t rom_report` y `west build     -d build/mcuboot -t rom_report`) confirman:
    - El reporte principal da `Root 115408`, es decir, la aplicación ocupa 115408 B  de los 225280 B disponibles en el slot
    - El bootloader 
        - Devuelve `Root 32000`, es decir, ocupa 32000 B de los 49152 B reservados
        - Está incorporada la clave pública demo
        - Usa RSA para verificar las firmas
        - Contiene código de validación de imágenes
        - Incluye la lógica de swap y rollback
        - Reconoce ambos slots
        - Puede leer, escribir y borrar flash

5. Se agregan las configuraciones necesarias al `prj.conf`


----

`rm -rf build`

`west build -b joya_nrf52 . --sysbuild -- -DBOARD_ROOT=$PWD 2>&1 | tee build_joya.log`

`west flash -d build --runner jlink --erase`

----
1. Compilar el proyecto con la clave privada

    `west build   -d build_joya   -b joya_nrf52/nrf52832   . --sysbuild   -- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"(...)/joya_dev-private.pem\"`

2. Unir el MCUBoot y la app

    `srec_cat \
    build_joya/mcuboot/zephyr/zephyr.hex -Intel \
    build_joya/JoyaPhoneConnectionFirmware/zephyr/zephyr.signed.hex -Intel \
    -o build_joya/merged.hex -Intel`

    `head build_joya/merged.hex`

    `srec_info build_joya/merged.hex -Intel`

3. Cargar el archivo completo
    `JLinkExe -device nRF52832_xxAA -if SWD -speed 4000`

    `erase`

    `loadfile build_joya/merged.hex`

    `r`
    `g`
    `q`

NOTA: para las actualizaciones por OTA, utilizar el archivo `zephyr.signed.bin

----
