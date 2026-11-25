# Joya — Seguimiento de auditoría de firmware

**Última actualización:** 24/11/2026
**Auditoría base:** v2.2.4, Antigravity, 24/11/2026.
**Imagen candidata:** v2.3.1+0.
**Plataforma:** nRF52832, Zephyr 4.2.0, MCUboot y BLE OTA.

## 1. Criterios de seguimiento

- **Pendiente:** todavía no se ha implementado una corrección documentada.
- **Implementado:** cambio presente y compilado, sin validación experimental completa.
- **Mitigado parcialmente:** se reduce el riesgo, pero quedan escenarios sin cubrir.
- **Postergado:** decisión explícita de abordar el punto posteriormente.
- **Validado:** prueba experimental satisfactoria, con evidencia registrada.
- **Cerrado:** criterios de aceptación cumplidos y evidencia revisada.

La severidad original se conserva para garantizar la trazabilidad.

Los hallazgos de análisis estático no equivalen a fallas reproducidas
experimentalmente.

Los resultados de compilación y comparación binaria provienen de los
informes de Codex y Antigravity.

### Restricciones fundamentales

Las unidades ensambladas carecen de acceso SWD. Todas las actualizaciones
de firmware se realizan mediante BLE OTA.

Por lo tanto:

- No modificar MCUboot ni su configuración.
- No modificar el particionamiento de Flash.
- No modificar las claves de firma.
- No modificar el mecanismo de swap.
- Preservar la capacidad de actualización mediante BLE OTA.
- Validar los cambios primero sobre la nRF52 DK.
- No desplegar cambios que puedan comprometer la recuperabilidad de las
  unidades ensambladas.

El testing debe ser controlado. Joya no debe utilizarse como único
mecanismo de seguridad personal mientras existan bloqueantes funcionales.

## 2. Auditoría original — Matriz de seguimiento

| ID | Severidad | Hallazgo | Estado |
|---|---|---|---|
| N-01 | Crítica | No se restaura emergencia pendiente ni reconexión automática tras reinicio. | Pendiente |
| N-02 | Crítica | Triple clic descartado durante suspensión y batería crítica. | Pendiente |
| N-03 | Crítica | Ausencia de watchdog, protección de stacks y recuperación insuficiente ante fallos. | Mitigado parcialmente |
| N-04 | Crítica | Pérdida de desconexión deja FSM autenticada e inconsistente. | Pendiente |
| N-05 | Alta | Debounce de 50 ms puede fusionar clics rápidos. | Pendiente |
| N-06 | Alta | Ventana OTA Test comparte flag de batería crítica y anuncia nombre incorrecto. | Pendiente |
| N-07 | Alta | Posible pérdida de configuración DRV2605, bloqueo I2C, carreras y suspensión incompleta. | Pendiente |
| N-08 | Alta | Escritura NVS desde BT_RX y carrera en el buffer del nombre. | Pendiente |
| N-09 | Alta | upload_complete bloquea factory reset y posible cierre fallido de SMP. | Pendiente |
| N-10 | Alta | Privacidad RF: nombre personal, MAC estática e iBeacon diferenciado. | Pendiente |
| N-11 | Alta | Consumo por advertising rápido, LDO, LFRC y otros factores. | Pendiente |
| N-12 | Media | Ventana Setup se interrumpe tras conexión fallida. | Pendiente |
| N-13 | Media | Diagnóstico inicial puede perderse por mutex K_NO_WAIT. | Pendiente |
| N-14 | Media | SAADC negativo y votación inicial inconclusa. | Pendiente |
| N-15 | Media | Endurecimiento OTA: límite de imagen, downgrade, claves y sectores NVS. | Pendiente |
| N-16 | Baja | Código residual y configuración redundante. | Pendiente |
| D-01 | Alta | APP_ID en claro, sin límite de intentos y aceptación de identificador nulo. | Pendiente |
| D-02 | Alta | Cola K_NO_WAIT permite perder eventos críticos. | Pendiente |
| D-03 | Media | Deshabilitación CCC no gestionada. | Pendiente |
| D-04 | Media | Confirmación háptica aunque la notificación de rutina falle. | Pendiente |

## 3. N-03 — Watchdog y recuperación ante fallos

### 24/11/2026 — Primera implementación

Se solicitó a Antigravity un análisis independiente de la arquitectura
de recuperación ante fallos, priorizando la compatibilidad con las
unidades ensambladas sin acceso SWD.

Posteriormente, Codex implementó:

- Watchdog hardware.
- Supervisor periódico.
- Supervisión de progreso de main y FSM.
- MPU y protección hardware de stacks.
- Reinicio ante errores fatales y assertions del controlador BLE.

La implementación fue auditada nuevamente por Antigravity.

### 24/11/2026 — Segunda auditoría

Se identificaron las siguientes observaciones:

| ID | Hallazgo | Resolución |
|---|---|---|
| B-01 | Dependencia de includes transitivos para NVIC_SystemReset(). | Corregido. |
| B-02 | Igualdad rígida de CRV, RREN y CONFIG ante watchdog heredado. | Configuración fija documentada. Flexibilidad postergada. |
| B-03 | Reinicios rápidos ante fallos persistentes de inicialización. | Mitigado parcialmente. |
| R-01 | Posible margen insuficiente de stacks con MPU habilitada. | MPU postergada. Medición pendiente. |
| R-02 | Diferencias entre rollback y downgrade hacia versiones sin WDT. | Documentado. Pendiente de pruebas. |
| R-03 | Ausencia de advertising automático y modo seguro tras reset. | Pendiente. |

Otras observaciones:

- Utilizar aritmética de 64 bits para calcular WDT_CRV_TICKS.
- Encapsular el acceso a event_queue.
- Evaluar CONFIG_BT_ASSERT_PANIC.
- Separar los cambios de iBeacon de los cambios de recuperación al
  realizar commits.

### 24/11/2026 — Implementación acotada final

Se decidió reducir el alcance de la implementación para minimizar
riesgos durante las próximas rondas de testing.

#### Configuración del watchdog

- Watchdog hardware WDT0.
- Timeout: 30 segundos.
- Canal único RR[0].
- Supervisor cada 10 segundos.
- Ejecución del supervisor en system workqueue.
- Watchdog activo durante sleep.
- Watchdog pausado cuando el procesador se encuentra detenido por
  el depurador.

#### Decisiones de arquitectura

- Mantener fija la configuración hardware del watchdog entre versiones.
- No implementar todavía aceptación dinámica de configuraciones heredadas.
- No modificar MCUboot.
- No modificar el mecanismo de rollback.
- No modificar el protocolo BLE.
- Postergar MPU y protección hardware de stacks.
- Mantener los tamaños actuales de stacks.
- Postergar contador de reinicios consecutivos.
- Postergar publicidad automática tras reinicios.
- Postergar modo seguro de recuperación.

## 4. Cambios implementados

### src/wdt_guard.c y include/wdt_guard.h

Se incorporó un módulo independiente para gestionar el watchdog.

Responsabilidades:

- Inicialización del watchdog hardware.
- Verificación de la configuración heredada.
- Alimentación periódica del watchdog.
- Supervisión de finalización de main().
- Supervisión del progreso de la FSM.
- Detección de eventos cuyo procesamiento supera los 15 segundos.
- Detección de falta de progreso con eventos pendientes.
- Supervisión indirecta de la system workqueue.

El supervisor distingue entre una FSM bloqueada procesando un evento
y una FSM legítimamente ociosa esperando en K_FOREVER.

Si la system workqueue se bloquea, el trabajo periódico deja de
ejecutarse y el watchdog deja de alimentarse.

Se incorporó aritmética de 64 bits para WDT_CRV_TICKS.

#### Fallo de inicialización del watchdog

Si el watchdog ya se encuentra activo y su configuración es incompatible,
el firmware permanece despierto sin alimentarlo hasta que expire.

La espera activa evita que una configuración heredada que pause el
watchdog durante sleep impida el reset.

Si no existe un watchdog activo y su inicialización falla, el firmware
continúa el arranque sin supervisión hardware.

Motivo:

- Evitar una espera indefinida de un reset que nunca ocurrirá.
- Preservar una posible vía de recuperación mediante BLE OTA.

Limitación:

- Esa sesión queda sin protección hardware del watchdog.

### src/app_state.c y include/app_state.h

Se incorporaron:

- Marcadores de inicio y finalización del procesamiento de eventos.
- Función app_state_pending_events().

El supervisor consulta la ocupación de la cola mediante la nueva
interfaz, evitando acceder directamente a event_queue.

No se modificó la lógica funcional de la FSM.

### src/main.c

Se incorporó la inicialización del watchdog y la notificación de
finalización del arranque mediante wdt_guard_main_ready().

El supervisor solamente alimenta el watchdog cuando main() informa
que la inicialización está completa.

#### Fallo de inicialización BLE en imagen confirmada

Si el watchdog está activo:

- No se informa que main() terminó correctamente.
- La CPU permanece en reposo.
- Se deja expirar el watchdog para provocar un nuevo intento de arranque.

Si el watchdog no está activo:

- Se espera 30 segundos.
- Se ejecuta un reset por software.

Esto evita ciclos de reinicios rápidos.

Limitación:

- Un fallo persistente de BLE sigue pudiendo impedir la recuperación
  mediante OTA.

#### Fallo de inicialización del botón en imagen confirmada

- Se registra DIAG_INIT_BUTTON.
- Se continúa con el arranque degradado.

Limitación:

- Si el botón no funciona y el dispositivo no inicia advertising
  automáticamente, la recuperación mediante BLE no está garantizada.

#### Otros casos

- Se conserva la política de reinicio y rollback de imágenes Test.
- Se conserva el comportamiento de inicialización de háptica.
- Se conserva el comportamiento de inicialización de batería.

### src/fatal_recovery.c

Se incorporaron manejadores para:

- Errores fatales de Zephyr.
- Assertions del controlador BLE.

El MCU se reinicia mediante NVIC_SystemReset().

Se agregó explícitamente:

    #include <cmsis_core.h>

El watchdog permanece como mecanismo de recuperación de último recurso.

### prj.conf

Se habilitaron:

- Watchdog hardware.
- Driver nrfx correspondiente.
- Handler de assertions del controlador BLE.

Se revirtieron exclusivamente los cambios de activación de MPU y
protección hardware de stacks.

No se modificaron los tamaños de stacks.

La postergación de MPU es una decisión preventiva y no implica que
su implementación sea incorrecta.

### CMakeLists.txt

Se incorporaron los nuevos módulos:

- src/wdt_guard.c
- src/fatal_recovery.c

### docs/RECOVERY_VALIDATION.md

Se documentaron:

- Pruebas de recuperación sobre DK.
- Diferencias entre rollback y downgrade.
- Limitaciones de arranque degradado.
- Medición pendiente de stacks.
- Compatibilidad de versiones con y sin watchdog.

## 5. Compatibilidad del watchdog entre versiones

El watchdog del nRF52832 presenta la particularidad de que los
registros CRV, RREN y CONFIG quedan bloqueados mientras está activo.

Estos registros conservan su configuración tras un soft reset.

Por lo tanto, las próximas versiones deberán:

- Mantener la configuración hardware actual, o
- Implementar explícitamente compatibilidad con configuraciones heredadas.

La configuración fija actual no impide incorporar posteriormente una
implementación más flexible mediante OTA.

No será necesario modificar MCUboot para incorporar esa lógica en la
aplicación, siempre que la transición se diseñe y valide correctamente.

### Rollback automático hacia una versión sin watchdog

Escenario:

1. Imagen anterior sin watchdog confirmada.
2. OTA hacia una imagen Test con watchdog.
3. La imagen Test no se confirma.
4. Se ejecuta el rollback.
5. La imagen anterior puede heredar un watchdog activo.
6. Al no alimentarlo, podría producirse un reset adicional.
7. Se espera que la imagen anterior quede estable posteriormente.

Estado: pendiente de validación experimental.

### Downgrade voluntario hacia una versión sin watchdog

Escenario:

1. Imagen con watchdog confirmada.
2. OTA hacia una versión anterior sin watchdog.
3. La versión anterior arranca en modo Test.
4. Hereda un watchdog activo.
5. Si el watchdog expira antes de confirmar la imagen, puede producirse
   un rollback hacia la versión con watchdog.

Estado: pendiente de validación experimental.

No se modificó el mecanismo existente de confirmación y rollback para
resolver estos escenarios.

## 6. Evidencia de compilación

Resultados reportados por Codex el 24/11/2026.

### Compilación

- Target: joya_nrf52/nrf52832.
- Resultado: satisfactorio.
- tests/run_emergency_phase.sh: satisfactorio.
- git diff --check: satisfactorio.

### MCUboot

El bootloader generado coincide byte a byte con la región de MCUboot
de todos los HEX históricos revisados.

SHA-256 de la región:

    52df2db4d4b64db274abb61676a4987546d7386662cbb2930bd4286906591332

No se modificó MCUboot ni su configuración.

El bootloader histórico dispone de mecanismos de alimentación del
watchdog durante el arranque, borrado y copia de imágenes.

### Particionamiento

| Región | Offset | Tamaño |
|---|---|---|
| MCUboot | 0x00000 | 0x0C000 |
| Slot 0 | 0x0C000 | 0x37000 |
| Slot 1 | 0x43000 | 0x37000 |
| Storage | 0x7A000 | 0x06000 |

Resultado: sin modificaciones.

### Memoria

| Recurso | Antes | Después | Incremento |
|---|---:|---:|---:|
| Flash | 161620 B | 164848 B | 3228 B |
| SRAM | 45348 B | 45476 B | 128 B |

Capacidad de Flash reportada para la aplicación: 216752 B.

Capacidad SRAM: 65536 B.

### Stacks

| Contexto | Tamaño |
|---|---:|
| main | 4096 B |
| System workqueue | 4096 B |
| BT_RX | 1200 B |
| MCUmgr workqueue | 2048 B |

No se modificaron los tamaños.

### Artefacto OTA

- Archivo: zephyr.signed.bin.
- Versión: 2.3.1+0.
- Tamaño: 165184 B.
- Firma: verificada mediante imgtool verify.

SHA-256:

    7871c9b2777b4501eb776ef0b9483bbea551b20f83541b7d3f0323addcd37aa9

La firma fue validada con la clave de desarrollo correspondiente
al bootloader histórico.

Antes de actualizar unidades físicas, comprobar que la clave utilizada
es la correspondiente a dichas unidades.

No se realizaron operaciones de flasheo ni OTA durante esta etapa.

## 7. Limitaciones conocidas

El watchdog no garantiza detectar:

- Inconsistencias lógicas de la FSM.
- FSM detenida mientras la cola permanece vacía.
- Bloqueos internos de BLE que no afectan los contextos supervisados.
- Fallos físicos de hardware.
- Errores recurrentes durante el arranque.
- Pérdida de eventos críticos.

El supervisor se ejecuta cada 10 segundos.

El umbral de procesamiento de eventos es de 15 segundos.

El reset ocurre cuando expira el watchdog desde su última alimentación,
no necesariamente 30 segundos después de producirse el fallo.

Una operación legítima excepcionalmente larga podría provocar un falso
positivo.

Sin MPU permanece el riesgo de corrupción silenciosa de memoria ante
desbordamientos de stack.

La recuperación mediante BLE después de un reset depende actualmente
del funcionamiento del botón y de la lógica de advertising existente.

## 8. Validación experimental sobre nRF52 DK

### Requisitos

- Utilizar el MCUboot histórico.
- Utilizar imágenes firmadas con la clave correspondiente.
- Registrar versiones iniciales y finales.
- Registrar logs y comportamiento observado.
- Utilizar imágenes Test antes de confirmar.
- No distribuir builds con fallos deliberadamente inyectados.

### Checklist

- [ ] OTA desde versión sin watchdog hacia versión con watchdog.
- [ ] Arranque y confirmación de imagen con watchdog.
- [ ] Segunda OTA entre versiones con watchdog.
- [ ] Rollback automático hacia versión sin watchdog.
- [ ] Verificación de posible reset adicional tras rollback.
- [ ] Downgrade voluntario hacia versión sin watchdog.
- [ ] OTA interrumpida.
- [ ] Recuperación y reintento después de OTA interrumpida.
- [ ] Funcionamiento normal prolongado.
- [ ] Suspensión lógica.
- [ ] Batería crítica.
- [ ] Bloqueo deliberado de FSM mediante loop.
- [ ] Bloqueo deliberado de FSM mediante espera bloqueante.
- [ ] Bloqueo deliberado de system workqueue.
- [ ] Error fatal de Zephyr.
- [ ] Assertion del controlador BLE.
- [ ] Fallo de inicialización BLE.
- [ ] Fallo de inicialización del botón.
- [ ] Medición de stacks durante BLE, OTA y NVS.

### Registro de resultados

| Fecha | Prueba | Versión | Resultado | Observaciones |
|---|---|---|---|---|
| | | | Pendiente | |

### Estado actual de aprobación

- Pruebas sobre DK: autorizadas.
- Actualización de unidades ensambladas: pendiente de validación.
- Producción: no autorizada.

La compilación satisfactoria y la identidad del bootloader no
constituyen por sí solas una validación de la recuperabilidad OTA.

## 9. Pendientes específicos de N-03

- [ ] Validación experimental del watchdog.
- [ ] Validación experimental de recuperación ante errores fatales.
- [ ] Validación de transiciones OTA entre versiones.
- [ ] Validación de rollback hacia versiones sin watchdog.
- [ ] Validación de downgrade hacia versiones sin watchdog.
- [ ] Medición del uso máximo de stacks.
- [ ] Evaluación del margen disponible en BT_RX.
- [ ] Evaluación del margen disponible en mcumgr_wq.
- [ ] Incorporación posterior de MPU.
- [ ] Incorporación posterior de protección hardware de stacks.
- [ ] Evaluación de configuraciones WDT heredadas flexibles.
- [ ] Evaluación de contador de reinicios consecutivos.
- [ ] Evaluación de modo seguro de recuperación.
- [ ] Evaluación de advertising automático tras reinicios.
- [ ] Evaluación específica de CONFIG_BT_ASSERT_PANIC.

## 10. Próximas prioridades de la auditoría

### Prioridad inmediata

1. Validar el watchdog y la recuperabilidad OTA sobre DK.
2. Resolver N-09: bloqueo del factory reset por OTA.
3. Resolver conjuntamente N-04 y D-02: pérdida de eventos críticos,
   desincronización BLE/FSM y autorización por conexión.

### Confiabilidad de emergencias

4. Resolver N-01: restauración del estado persistido tras reinicio.
5. Resolver N-02: emergencia durante suspensión y batería crítica.
6. Resolver N-05: detección confiable de pulsaciones rápidas.

### Mejoras posteriores

- Continuar con los hallazgos restantes.
- Medir y optimizar consumo energético.
- Endurecer autenticación y privacidad BLE.
- Revisar concurrencia y arquitectura.
- Limpiar código residual.

## 11. Organización del trabajo

- Codex: implementación de modificaciones acordadas.
- Antigravity: auditoría independiente.
- Desarrollador: decisiones de arquitectura y validación experimental.

Las auditorías deben conservar independencia respecto de la
implementación.

No se deben implementar automáticamente todas las recomendaciones
del auditor sin evaluar sus implicaciones.

Los cambios deben mantenerse acotados y preservar la posibilidad de
actualización OTA.

## 12. Plantilla para registrar nuevos avances

### AAAA-MM-DD — Título de la modificación

**Hallazgos asociados:**

**Versión / rama / commit:**

**Estado anterior:**

**Cambios implementados:**

- Archivo:
- Función:
- Comportamiento anterior:
- Comportamiento nuevo:

**Motivación y riesgos mitigados:**

**Decisiones técnicas:**

**Posibles efectos secundarios:**

**Evidencia de compilación:**

**Pruebas automatizadas:**

**Validación experimental:**

**Compatibilidad con MCUboot y OTA:**

**Estado actualizado del hallazgo:**

**Limitaciones conocidas:**

**Pendientes y próximas acciones:**

## 13. Fuentes

- AUDITORIA_FIRMWARE_JOYA.pdf: auditoría integral de Antigravity,
  v2.2.4, 24/11/2026.
- Propuesta técnica N-03 de Antigravity, 24/11/2026.
- Informe de primera implementación de Codex, 24/11/2026.
- Segunda auditoría de Antigravity, 24/11/2026.
- Informe final de implementación acotada de Codex, 24/11/2026.
- docs/RECOVERY_VALIDATION.md.

Este documento constituye un registro evolutivo de avances y no
sustituye la auditoría original ni los resultados experimentales.