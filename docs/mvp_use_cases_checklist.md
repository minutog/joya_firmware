# Checklist MVP Joya - funcionamiento y casos de uso

Este documento es una guia corta de comportamiento para el MVP de Joya. No es una especificacion tecnica de BLE, tiempos, pines ni protocolo interno. Es
un checklist de logica de producto.

## 1. Idea general

- [ ] Joya tiene que sentirse simple: si ya esta vinculada, el usuario no deberia
  pensar en conexion.
- [ ] La app y Joya tienen que reencontrarse solas cuando vuelven a estar cerca o
  cuando vuelve Bluetooth.
- [ ] El boton cambia de significado segun el estado, pero para el usuario tiene
  que sentirse consistente.
- [ ] La emergencia siempre tiene prioridad.
- [ ] Las vibraciones tienen que explicar que paso sin obligar a mirar el celular.

## 2. Estados del producto

| Estado | Que significa | Que puede pasar | Como sale |
| --- | --- | --- | --- |
| Nueva / sin vincular | Joya todavia no pertenece a ningun celular | El usuario puede abrir el modo setup con doble click | Se vincula con la app |
| Setup abierto | Joya esta visible para que la app la encuentre | La app la detecta y la vincula; si no pasa nada, vuelve a quedar quieta | Vinculacion exitosa o se cierra solo |
| Vinculada, sin conexion activa | Joya ya pertenece a una app pero el celular no esta conectado en ese momento | Joya y la app intentan reencontrarse solas | Se reconecta |
| Conectada | Joya y la app se estan escuchando | Se pueden iniciar rutinas, cancelar rutinas, recibir avisos y disparar emergencia | Se desconecta, se resetea vinculacion o entra emergencia |
| Rutina | La app entiende que hay una rutina activa | Un nuevo click vuelve a mandar inicio y vuelve a vibrar; no queda bloqueado esperando cancelacion | La app o el usuario cancelan, o aparece emergencia |
| Emergencia | La prioridad maxima esta activa | Joya insiste hasta que la app se entere; otros usos normales quedan bloqueados | Solo se apaga desde la app |
| Reset de vinculacion | El usuario borra la relacion con el celular | Se corta la conexion y Joya vuelve a quedar como nueva | Vuelve a estado sin vincular |

## 3. Vinculacion inicial

- [ ] Una Joya nueva no deberia estar siempre visible.
- [ ] El usuario abre setup con doble click.
- [ ] Joya vibra para confirmar que entro en setup.
- [ ] La app busca a Joya y la vincula.
- [ ] Si la vinculacion sale bien, Joya queda asociada a esa app.
- [ ] Si la vinculacion se interrumpe antes de terminar, el usuario puede volver a
  intentar desde la app o haciendo doble click de nuevo.
- [ ] Si Joya ya estaba vinculada y el usuario intenta vincularla como si fuera
  nueva, la app no deberia mostrar un error raro. Para MVP, puede tratarlo como
  "ya esta conectada". Si no la encuentra, pedir que acerque Joya y vuelva a
  intentar.

## 4. Reconexion y cortes

- [ ] Si el celular se aleja y despues vuelve, la conexion deberia volver sola.
- [ ] Si Bluetooth se apaga y despues se prende, Joya deberia reconectarse sin que
  el usuario tenga que entender que paso.
- [ ] Si la app no encuentra a Joya, el mensaje deberia ser simple: acercar Joya,
  revisar Bluetooth y volver a intentar.
- [ ] Si se corta la conexion durante setup, Joya puede seguir disponible mientras
  el modo setup siga abierto.
- [ ] Si se corta la conexion despues de estar vinculada, Joya no pierde la
  vinculacion.
- [ ] Una reconexion no deberia duplicar acciones viejas: no deberia reiniciar una
  rutina ni cancelar una emergencia por accidente.

## 5. Boton segun estado

| Estado | 1 click | 2 clicks | 3 clicks | Mantener apretado | Mantener mucho tiempo |
| --- | --- | --- | --- | --- | --- |
| Sin vincular | No hace nada | Abre setup | Emergencia | No hace nada | Reset/vuelve a sin vincular |
| Setup abierto | No hace nada | Refuerza setup | Emergencia | No hace nada | Reset/vuelve a sin vincular |
| Vinculada pero desconectada | No hace nada | Ayuda a que la app la encuentre | Emergencia | No hace nada | Reset/vuelve a sin vincular |
| Conectada | Inicia rutina | No hace nada | Emergencia | Cancela rutina | Reset/vuelve a sin vincular |
| Rutina | Vuelve a iniciar/avisar rutina y vibra otra vez | No hace nada | Emergencia | Cancela rutina | Reset/vuelve a sin vincular |
| Emergencia | No hace rutina | No hace setup | Repite vibracion de emergencia | No cancela emergencia | No es forma de salir de emergencia |

Reglas importantes:

- [ ] Un click sin conexion no inicia rutina.
- [ ] Doble click conectado no abre setup.
- [ ] Si una rutina ya estaba activa, otro click puede volver a mandar inicio y
  volver a vibrar. No hace falta esperar a cancelar.
- [ ] Triple click dispara emergencia desde cualquier estado.
- [ ] Durante emergencia, los gestos normales quedan bloqueados.
- [ ] Durante emergencia, repetir triple click vuelve a vibrar como confirmacion.

## 6. Rutina

- [ ] La rutina vive conceptualmente en la app. El firmware solo avisa que el
  usuario apreto el boton.
- [ ] 1 click conectado significa "iniciar rutina" o "avisar rutina otra vez".
- [ ] Si la rutina ya estaba activa y el usuario aprieta de nuevo, se vuelve a
  sentir la misma vibracion.
- [ ] Mantener apretado conectado significa cancelar rutina.
- [ ] La emergencia siempre pasa por encima de rutina.
- [ ] Si Joya esta desconectada, no guarda clicks de rutina para mandarlos despues.

## 7. Emergencia

- [ ] Triple click inicia emergencia en cualquier estado.
- [ ] Joya vibra inmediatamente, aunque el celular no este conectado.
- [ ] Si el celular no se entero todavia, Joya sigue intentando avisarle.
- [ ] Cuando la app se entera, Joya deja de insistir, pero sigue en emergencia.
- [ ] La emergencia no se apaga por otro click, por una rutina, por desconexion ni
  por reconexion.
- [ ] La emergencia solo se cierra desde la app.
- [ ] No hay un flujo normal donde el usuario "rebootea" Joya para salir de
  emergencia.
- [ ] Mientras emergencia esta activa, los avisos de amigo y la rutina no deben
  cambiar el estado de emergencia.

## 8. Avisos desde el celular

- [ ] La app puede mandar un aviso de "tu amigo viene / te sigue".
- [ ] Joya responde con una vibracion en la muneca.
- [ ] Ese aviso no cambia ningun estado.
- [ ] No inicia emergencia.
- [ ] No cancela rutina.
- [ ] No desbloquea ni bloquea botones.
- [ ] Es solo un mensaje recibido desde el celular.

## 9. Vibraciones

No documentamos aca duraciones ni valores. Solo la intencion.

- [ ] Setup: "Joya se desperto y esta lista para vincular".
- [ ] Vinculacion exitosa: "quedo conectada".
- [ ] Rutina: "se registro la accion".
- [ ] Cancelacion: "se cerro/cancelo".
- [ ] Emergencia: "alerta importante".
- [ ] Amigo en camino: "recibiste respuesta del celular".
- [ ] Reset de vinculacion: "se borro la relacion con el celular".

## 10. Casos que no nos podemos olvidar

- [ ] Joya nueva: doble click, app la encuentra, queda vinculada.
- [ ] Joya nueva: doble click, no pasa nada, despues se puede intentar de nuevo.
- [ ] Se corta setup antes de terminar: se puede reintentar sin romper nada.
- [ ] Joya ya vinculada: al acercar el celular, reconecta sola.
- [ ] Bluetooth se apaga y vuelve: reconecta sola.
- [ ] App no encuentra Joya: pedir acercarla y reintentar, no mostrar error tecnico.
- [ ] 1 click conectado: app recibe inicio de rutina y Joya vibra.
- [ ] 1 click en rutina: vuelve a mandar inicio y vuelve a vibrar.
- [ ] Mantener apretado conectado: cancela rutina.
- [ ] 1 click desconectado: no hace nada.
- [ ] Doble click desconectado: ayuda a que la app la encuentre.
- [ ] Triple click conectado: emergencia inmediata.
- [ ] Triple click desconectado: emergencia inmediata y aviso pendiente para la app.
- [ ] App confirma emergencia: Joya deja de insistir pero sigue en emergencia.
- [ ] App cierra emergencia: Joya vuelve a uso normal.
- [ ] App manda amigo en camino: Joya vibra, sin cambiar estado.
- [ ] Reset de vinculacion: Joya se olvida del celular y vuelve a empezar.
