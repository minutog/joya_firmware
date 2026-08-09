# Herramientas para host Windows

1. Abrir la terminal WSL
2. Iniciar Docker (modificar las rutas)
```
sudo docker run --rm -it \
  -v "RUTA/A/JOYAPHONECONNECTIONFIRMWARE":/workspace/app \
  -v "RUTA/A/KEYS:/workspace/keys:ro" \
  joya-dev
```
3. (Opcional) Abrir Visual Studio Code y adjuntar el contenedor (Ctrl + Shift + P -> Attach running container...)

### Primer flash
Notas: los comandos esta diseñado para ejecutarse desde JoyaPhoneConnectionFirmware

1. Compilar (si se realizó el paso 3, se puede hacer desde la terminal de VSC)
```
west build -d build_joya -b joya_nrf52/nrf52832 . --sysbuild -- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"/workspace/keys/joya_dev-private.pem\"
```


2. Armar el .hex 
```
srec_cat \
  ./build_joya/mcuboot/zephyr/zephyr.hex -Intel \
  ./build_joya/app/zephyr/zephyr.signed.hex -Intel \
  -o ./build_joya/final_merged.hex -Intel
```

3. (Desde Windows) Grabar utilizando el JLinkCommander
```
device nRF52832_xxAA 
si SWD 
speed 4000
erase
loadfile COMPLETAR/RUTA/build_joya/final_merged.hex
r
g
q
```

### Actualización OTA
Una vez descargado el binario:
1. Abrir Device Manager
2. Ir a la segunda pestaña
3. En Firmware Manager, cargar el archivo
4. Presionar Start
5. Elegir Test and Confirm

# Archivos
Desde JoyaPhoneConnectionFirmware:
1. Bootloader + firmware
```
build_joya > merged_final.hex
```

2. Actualización OTA
```
build_joya > app > zephyr > zephyr.signed.bin
```
