#!/bin/bash

# 1. Definir la función (siempre se declara antes de llamarla)
mi_funcion() {
    local parametro_funcion="$1"
    echo "Dentro de la función procesando: $parametro_funcion"
}

# 2. Capturar el argumento con un VALOR POR DEFECTO
# Si $1 está vacío, toma el string "mi_valor_default"
ARGUMENTO="${1:-merged.hex}"

# 3. Llamar a la función pasándole el argumento
srec_cat \
  ./build_joya/mcuboot/zephyr/zephyr.hex -Intel \
  ./build_joya/app/zephyr/zephyr.signed.hex -Intel \
  -o ./files/"$ARGUMENTO" -Intel
