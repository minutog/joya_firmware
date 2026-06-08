# Checklist MVP Joya - funcionamiento y casos de uso

Este documento es la lista corta de lo que tiene que cumplir el MVP de firmware de
Joya desde uso y funcionamiento.

Leyenda:

- `[x]` ya esta contemplado en el MVP actual.
- `[ ]` falta implementar, probar o cerrar.
- `[?]` hay una decision de producto/firmware para confirmar.

## 2. Estados principales

| Estado | Como entra | Que permite | Que bloquea / ignora | Como sale |
| --- | --- | --- | --- | --- |
| Sin pairing | Boot sin `claimed` o reset de pairing | Doble click abre setup | 1 click no hace nada | Claim exitoso o nuevo doble click |
| Setup abierto | Doble click sin claim | Advertising como `Joya Setup` por 90s | Si expira, deja de anunciar | App conecta y manda claim |
| Claimed desconectado | Boot/desconexion con `claimed=true` | Advertising como `Joya` para reconectar | 1 click no inicia rutina | Celular conecta |
| Conectado | App conectada y notificaciones listas | Rutina, cancelacion, emergencia, mensajes app | Doble click no hace nada | Desconexion o reset |
| Rutina | 1 click conectado | App pasa a rutina; hoy el firmware no la persiste | Emergencia tiene prioridad | Hold o app cancelan |
| Emergencia | Triple click en cualquier estado | Retry hasta ACK, haptic inmediato | Rutina y holds normales | Solo app con `EMERGENCY_OFF` / `CANCEL_EMERGENCY` |
| Reset pairing | Hold de 15s | Borra claim y corta conexion | No debe dejar estados viejos vivos | Vuelve a sin pairing |

## 3. Conexion con el celular

### Primer uso / pairing

- [x] Al boot, si Joya no esta claimed, no empieza a anunciar sola.
- [x] Doble click abre la ventana de setup y vibra para confirmar.
- [x] Durante setup, Joya anuncia como `Joya Setup` por 90 segundos.
- [x] La app escanea por el servicio BLE del MVP, conecta, descubre canales RX/TX
  y activa notificaciones.
- [x] Joya manda `HELLO:JOYA:claimed=0|1` cuando las notificaciones quedan listas.
- [x] La app manda `PING`; Joya responde `PONG:claimed=0|1`.
- [x] Si `claimed=0`, la app manda `CLAIM:<app_id>`.
- [x] Si el claim se guarda bien, Joya responde `CLAIM_OK:JOYA-DEV-001` y queda
  claimed para futuros boots.
- [x] Si el claim ya existia, Joya responde `ERR:ALREADY_CLAIMED` y no pisa el
  `app_id`.
- [ ] Si falla guardar el claim, Joya responde `ERR:CLAIM_SAVE_FAILED` y la app
  deberia permitir reintentar.

### Cuando algo interrumpe la conexion

- [x] Si nadie conecta durante los 90 segundos de setup, Joya deja de anunciar.
  Para probar de nuevo hay que hacer doble click otra vez.
- [x] Si se corta la conexion y Joya ya estaba claimed, vuelve a anunciar como
  `Joya` para que la app reconecte.
- [x] Si se corta la conexion mientras setup sigue abierto y Joya no esta claimed,
  puede seguir anunciando como `Joya Setup` hasta que venza la ventana.
- [x] La app no depende del peripheral guardado: vuelve a escanear.
- [x] Si un intento de conexion tarda mas de 8 segundos, la app cancela ese intento
  y reinicia busqueda.
- [x] Si falla `didFailToConnect` o `didDisconnect`, la app vuelve a escanear
  mientras el auto-reconnect este activo.
- [ ] Si Bluetooth esta apagado o sin permiso, la app debe mostrar estado claro y
  dejar reintentar cuando vuelva a estar disponible.
- [?] Definir que hace la app si recibe `ERR:ALREADY_CLAIMED` pero el usuario cree
  que esta haciendo setup por primera vez. Para MVP puede seguir conectado; para
  producto real probablemente haya que pedir reset/re-pairing.

## 4. Boton y modos

Tiempos actuales:

- Debounce: 50 ms.
- Ventana para contar clicks: 600 ms.
- Hold para cancelar rutina: 900 ms.
- Hold para reset de pairing: 15 s.

| Contexto | 1 click | 2 clicks | 3 clicks | Hold 900 ms | Hold 15 s |
| --- | --- | --- | --- | --- | --- |
| Sin pairing / desconectado | Ignorado | Abre setup | Emergencia | Ignorado | Reset pairing |
| Setup abierto | Ignorado | Reabre/refuerza setup | Emergencia | Ignorado | Reset pairing |
| Claimed desconectado | Ignorado | Abre reconnect advertising | Emergencia | Ignorado | Reset pairing |
| Conectado | Inicia rutina | Ignorado | Emergencia | Cancela rutina | Reset pairing |
| Emergencia activa | Ignorado | Ignorado | Repite haptic y, si falta ACK, reinicia retry | Ignorado | Reset pairing |

Checklist:

- [x] El boton nunca debe mandar rutina si no hay celular conectado.
- [x] La emergencia tiene prioridad sobre cualquier otro modo.
- [x] Triple click dispara emergencia incluso si el celular no esta conectado.
- [x] En emergencia, los clicks de rutina y holds normales se ignoran.
- [x] En emergencia, triple click vuelve a vibrar. Si todavia no hubo ACK, tambien
  reinicia el camino de retry.
- [x] Doble click conectado no hace nada para evitar confundir setup con uso normal.
- [?] Definir si una rutina ya iniciada bloquea nuevos `EVENT:ROUTINE_START` hasta
  cancelar. Hoy el firmware no guarda `routine_active`; la app interpreta el modo.
- [ ] Probar tiempos con hardware real: que triple click no sea demasiado facil de
  disparar por accidente ni demasiado dificil cuando hace falta.

## 5. Mensajes y reintentos

### Eventos Joya -> app

| Evento | Cuando se manda | Retry | Resultado esperado |
| --- | --- | --- | --- |
| `EVENT:ROUTINE_START` | 1 click conectado | No | App marca rutina iniciada |
| `EVENT:ROUTINE_CANCEL` | Hold conectado | No | App sale de rutina |
| `EVENT:EMERGENCY_ON` | Triple click | Si, hasta ACK | App marca emergencia y responde ACK |
| `EVENT:PHONE_PAIRING_RESET` | Hold 15s conectado | No | App borra estado local relacionado |

Reglas:

- [x] Rutina solo se manda cuando hay conexion. Si no hay conexion, el click se
  ignora y no queda en cola.
- [x] Emergencia vibra localmente antes de esperar respuesta del celular.
- [x] Si no hay conexion o no hay notificaciones, emergencia queda pendiente y Joya
  intenta anunciar para que la app reconecte.
- [x] Retry de emergencia: 1s, 2s, 5s, 10s y despues cada 30s.
- [x] Cuando la app manda `ACK:EMERGENCY_ON`, Joya deja de reenviar
  `EVENT:EMERGENCY_ON`, pero sigue en modo emergencia.
- [x] Emergencia se apaga solo cuando la app manda `EMERGENCY_OFF` o
  `CANCEL_EMERGENCY`.
- [?] Definir si la emergencia deberia sobrevivir un reboot o corte de bateria.
  Hoy no queda persistida.

### Comandos app -> Joya

| Comando | Resultado esperado |
| --- | --- |
| `PING` | Joya responde `PONG:claimed=0|1` |
| `CLAIM:<app_id>` | Guarda claim o responde error |
| `ACK:EMERGENCY_ON` | Corta retries de emergencia |
| `EMERGENCY_OFF` / `CANCEL_EMERGENCY` | Apaga emergencia y responde `ACK:EMERGENCY_OFF` |
| `FRIEND_COMING` / `FRIEND_COMING_FOR_YOU` | Vibra patron de amigo en camino y responde ACK |
| `CANCEL_ROUTINE` | Responde ACK, comportamiento local a terminar de definir |
| `HAPTIC_TEST` | Vibra patron de prueba y responde ACK |

Checklist:

- [x] Los mensajes repetidos no deberian romper estado. Ejemplo: un ACK repetido de
  emergencia no debe volver a disparar nada raro.
- [ ] Definir si `CANCEL_ROUTINE` desde la app solo es ACK o si tambien tiene que
  cambiar algun estado/haptic en Joya.
- [?] Revisar `FRIEND_COMING`: la regla escrita dice que no inicia emergencia ni
  retries. El codigo actual restaura un estado interno de emergencia acknowledged.
  Hay que decidir si eso es intencional o un resto de implementacion.

## 6. Haptics

Los patrones tienen que ser utiles sin mirar el celular. No hace falta que sean
largos; hace falta que sean distinguibles.

| Momento | Intencion del patron | Criterio de listo |
| --- | --- | --- |
| Doble click / setup | "Joya desperto y esta visible" | Se siente como apertura, no como alerta |
| Claim exitoso | "Quedo conectada/asociada" | Feedback corto y positivo |
| Rutina iniciada | "Se envio inicio de rutina" | Distinto de cancelacion y emergencia |
| Rutina cancelada | "Rutina cancelada" | Sensacion de cierre o apagado |
| Emergencia | "Alerta importante" | Fuerte, inmediato y reconocible |
| Amigo en camino / te sigue | "Tu amigo respondio" | Tranquilizador, no debe parecer emergencia |
| Reset pairing | "Se borro la asociacion" | Claro, pero no confundible con emergencia |
| Test haptic | "Hardware vibra" | Sirve para diagnostico, no para uso normal |

Checklist:

- [x] La emergencia vibra siempre al triple click, incluso sin celular.
- [x] `FRIEND_COMING` dispara un patron propio y responde ACK.
- [x] Setup tiene un patron de wake.
- [x] Rutina start/cancel tienen patrones distintos.
- [ ] Probar patrones con carcasa y actuador reales. Los valores actuales son de
  MVP, no calibracion final.
- [ ] Documentar una tabla final de patrones cuando esten aprobados por uso real.

## 7. Casos de uso minimos para validar

| ID | Caso | Esperado |
| --- | --- | --- |
| UC-01 | Joya nueva, doble click, app conecta | Setup abre, app hace claim, Joya queda claimed |
| UC-02 | Joya nueva, doble click, nadie conecta | A los 90s deja de anunciar; doble click reintenta |
| UC-03 | Pairing se corta antes de claim | Vuelve a setup si la ventana sigue abierta; si expiro, doble click |
| UC-04 | Claim se corta justo despues de guardar | Al reconectar, Joya responde `claimed=1` |
| UC-05 | App intenta claim duplicado | Joya responde `ERR:ALREADY_CLAIMED` sin borrar datos |
| UC-06 | Claimed, boot normal | Joya anuncia como `Joya`; app reconecta por scan |
| UC-07 | Desconexion del celular | Joya vuelve a anunciar y app reintenta |
| UC-08 | 1 click conectado | App recibe rutina start y Joya vibra rutina |
| UC-09 | Hold conectado | App recibe rutina cancel y Joya vibra cancel |
| UC-10 | 1 click desconectado | No pasa nada |
| UC-11 | Doble click desconectado | Abre advertising segun corresponda |
| UC-12 | Triple click conectado | Vibra emergencia, app recibe evento y responde ACK |
| UC-13 | Triple click desconectado | Vibra emergencia, queda pendiente y se entrega al reconectar |
| UC-14 | Emergencia sin ACK | Reintenta con backoff hasta recibir ACK |
| UC-15 | Emergencia ACKed | Deja de reintentar, pero sigue en emergencia |
| UC-16 | App cancela emergencia | Joya limpia emergencia y responde ACK |
| UC-17 | App manda amigo en camino | Joya vibra patron friend-coming y responde ACK |
| UC-18 | Hold 15s | Borra claim, desconecta y vuelve a esperar doble click |

## 8. Puntos abiertos que conviene cerrar antes de handoff

- [?] Confirmar si `FRIEND_COMING` debe tocar estado de emergencia o solo vibrar.
- [?] Definir si `ERR:ALREADY_CLAIMED` en setup se trata como exito, error o
  pantalla de reset.
- [?] Definir comportamiento exacto de `CANCEL_ROUTINE` desde la app.
- [?] Definir si `routine_active` vive solo en la app o tambien en firmware.
- [?] Decidir si emergencia debe persistir ante reboot.
- [ ] Validar en hardware real los tiempos de boton y los patrones hapticos.
- [ ] Probar la matriz de reconexion con iPhone real: Bluetooth off/on, app cerrada,
  interrupcion durante claim, desconexion y emergencia pendiente.
