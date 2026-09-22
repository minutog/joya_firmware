- Revisar prioridades de vibración (emergencia > a todo)

--
> TODO — Prioridad para vibraciones pendientes

Actualmente, mientras el NBM5100 espera la señal RDY, solo se guarda un patrón háptico pendiente. Si llega otra solicitud durante ese intervalo, puede reemplazar la anterior.

A futuro, implementar un único patrón pendiente con prioridades:

- Factory reset y emergencia: prioridad máxima.
- Emergencia de un amigo: prioridad alta.
- Cancelación, follow-me y ACK: prioridad media.
- Inicio de rutina y setup: prioridad baja.

El nuevo patrón solo debe reemplazar al pendiente si tiene mayor prioridad. No reiniciar el contador de intentos ni el timeout cuando llegue otra solicitud.

No utilizar una cola FIFO completa: podría reproducir vibraciones atrasadas que ya no representen el estado actual.
--

- Revisar retries en esos casos

- Anuncio continuo luego de desconexión y luego del apagado -> ver tiempo máximo
- Agresivo y luego paso a más tiempo (por si la desconexión es accidental)
- Repetir el FRIEND_EMERGENCY cada 5 segundos hasta que haga el recibido -> ya agregado el COMMAND_FRIEND_STOP_EMERGENCY como 0x45