### 24/11/2026 — Validación OTA entre versiones con WDT

- Versión inicial: v2.3.1.
- Versión final: v2.3.2.
- Única modificación: VERSION.
- Método de actualización: BLE OTA.
- Resultado: SATISFACTORIO.

Pruebas realizadas:

- [x] Transferencia completa de firmware.
- [x] Reinicio y arranque de la nueva imagen.
- [x] Reconexión y autenticación BLE.
- [x] Confirmación de la imagen.
- [x] Ausencia de reinicios inesperados durante la observación.
- [x] Funcionamiento habitual del dispositivo.

Se valida experimentalmente la compatibilidad OTA entre
versiones con la misma configuración de watchdog hardware.

### 24/11/2026 — Validación de rollback entre versiones con WDT

- Versión inicial confirmada: v2.3.2.
- Imagen OTA Test: v2.3.3.
- Configuración WDT: idéntica en ambas versiones.
- Secuencia: Upload → Test → Reset → Sin Confirm.
- Timeout de confirmación: 5 minutos.
- Resultado: SATISFACTORIO.

Se verificó el rollback automático hacia v2.3.2
tras vencer el plazo de confirmación.

El watchdog no impidió la ejecución del mecanismo
de rollback de MCUboot.

### 24/11/2026 — Interrupción de OTA con watchdog activo

- Versión activa: v2.3.2.
- Imagen OTA: v2.3.3.
- Transferencia interrumpida antes de finalizar.
- Resultado: SATISFACTORIO hasta reconexión.

Pruebas realizadas:

- [x] Interrupción de transferencia OTA.
- [x] Reconexión BLE posterior.
- [x] Consulta de estado de imágenes.
- [x] La imagen incompleta no aparece en el estado reportado.
- [x] Se conserva la versión anterior.
- [x] Nueva transferencia completa y confirmación.

La interrupción de la transferencia no impidió
recuperar el acceso BLE al dispositivo.

### 24/11/2026 — Recuperación ante bloqueo de FSM

- Hardware: DK nRF52832.
- Método: inyección intencional de loop infinito.
- Disparador: triple clic.
- Tiempo observado hasta el reinicio: 45 segundos.
- Resultado: SATISFACTORIO.

Se verificó experimentalmente que el firmware se recupera
automáticamente ante un bloqueo de la máquina de estados.

El tiempo observado es consistente con los parámetros
del supervisor y del watchdog hardware.