# Improvements
---
1. Decidir qué fallos son catastróficos y cuáles permiten el normal funcionamiento

    *Ejemplo: si falla el almacenamiento en memoria flash, continuar con las variables en RAM y que el salto de persistencia se produzca al reiniciar el dispositivo*
    Posibles fallos no catastróficos: memoria flash y haptics

2. Decidir los reintentos de conexión y las consecuencias de no poder entregar un mensaje

    *Ejemplo: los envíos de comandos desde JOYA a la aplicación se producen únicamente cuando hay conexión. Si por alguna razón fallara el envío ¿cómo se procede? ¿se reintenta? ¿cuántas veces/por cuánto tiempo?*
    Hasta el momento la única ventana de conexión que existe es entre UNPAIRED y SETUP. Cuando se desconecta, envía ADV permanentemente. No se evalua el retorno de las funciones de escritura

3. Decidir si el dispositivo contará con algún método de reinicio completo (incluyendo microcontrolador)

    *Ejemplo: si JOYA está en emergencia y, por alguna razón, el celular no puede comunicarse con el app_id existente, no puede borrarse sin acceder al microcontrolador ya que el FACTORY_RESET es ignorado*

4. ¿Un FACTORY RESET reinicia también el microcontrolador?

5. ¿Tratar la deshabilitación de notificaciones?

6. Podría solicitarse el reenvío de alertas de emergencia mediante la queue para evitar condiciones de carrera

7. ¿El envío de app_id incorrectos tiene límite?

8. Si falla un haptic o un almacenamiento en flash ¿se reintenta?

9. ¿Qué pasa si la cola de eventos está llena?

# Decisiones de diseño actuales

- Si falla FLASH: sigue
- Si falla HAPTICS: sigue
- Si falla el envío de un mensaje: sigue

# Observaciones:

- Para el modo debug, descomentar las siguientes líneas en ```prj.conf```

    ``` #CONFIG_USE_SEGGER_RTT=y ```
    ```    #CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP=y```

- Para ver los logs, utilizar RTT. Por ejemplo, si se usa ```west```, ejecutar el comando ```west rtt```

- Una vez se salga del modo debug, comentarlas y volver a grabar el programa