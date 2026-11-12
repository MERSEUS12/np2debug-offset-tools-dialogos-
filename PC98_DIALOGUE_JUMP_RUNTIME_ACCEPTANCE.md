# NP2Debug Next — Prueba de aceptación runtime del salto de diálogos PC-98

## Objetivo

Comprobar en el emulador real que elegir un diálogo de otro punto del **script PC-98 activo** produce el comportamiento de «libro»:

1. el intérprete entra por el encabezado del bloque seleccionado;
2. procesa y presenta el texto destino;
3. alcanza el marcador `HA\\` normal de ese bloque;
4. el juego queda preparado para continuar con la entrada normal del usuario;
5. al avanzar una vez, continúa desde el destino y no desde el diálogo anterior.

Esta prueba no valida todavía saltos entre archivos/escenas distintos ni adaptadores de otros motores.

## Reglas de seguridad

- No ejecutar esta prueba sin autorización expresa para modificar el estado runtime del emulador.
- No reanudar, pausar, pulsar teclas, armar saltos ni cambiar memoria durante el preflight de solo lectura.
- No usar `dialogue_jump_arm` para esta prueba: es el mecanismo de bajo nivel heredado. Usar `dialogue_jump_request`.
- Probar un único destino por ejecución. No enviar una segunda solicitud mientras la primera siga activa.
- Si el estado termina en `FAILED`, no intentar otro salto. Revisar primero si el rollback devolvió el diálogo anterior.
- No probar destinos de otro archivo/escena: el adaptador actual solo soporta el script activo.

## Fase A — Preflight de solo lectura

Con el emulador en un diálogo normal y sin una operación universal en curso:

1. Consultar `emulator_status`.
2. Consultar `dialogue_current`.
3. Consultar `cpu_registers`.
4. Ejecutar `dialogue_catalog_refresh`.
5. Obtener una página con `dialogue_catalog(offset=0, limit=32, refresh=false)`; usar `next_offset` para continuar si hace falta.
6. Elegir un `dialogue_id` distinto del diálogo actual, del mismo script y con `truncated=false`.

Registrar:
- script/escena activa;
- ID y texto del diálogo actual;
- ID y texto del destino;
- si el emulador estaba pausado o en ejecución;
- si el destino pertenece al mismo script.

Si no hay diálogo actual, el catálogo está truncado o el destino pertenece a otro script, detenerse. No improvisar un localizador.

## Fase B — Ejecutar un salto autorizado

Esta fase sí modifica el estado del juego y requiere autorización explícita.

1. Tomar el valor `generation` de la última respuesta de `dialogue_catalog_refresh` (la misma generación con la que se obtuvo el ID).
2. Enviar `dialogue_jump_request(dialogue_id, catalog_generation)` una sola vez.
3. La respuesta positiva solo significa «solicitud aceptada», no «salto completado».
4. Consultar `dialogue_jump_status` hasta que el estado sea terminal:
   - `SUCCEEDED`: continuar con la comprobación visual.
   - `FAILED`: detenerse y revisar el error/rollback.
   - `ROLLING_BACK`: esperar a que termine; no enviar otra solicitud.
   - `CANCELLED`: no asumir que el destino se presentó.
5. En `SUCCEEDED`, exigir que los tres indicadores sean verdaderos:
   - `proof.state_verified`
   - `proof.presented`
   - `proof.continuation_ready`
6. Consultar otra vez `dialogue_current` y confirmar que su ID corresponde al destino.

## Fase C — Comprobación de pantalla y continuidad

Con el intérprete en la espera normal del diálogo destino (el emulador sigue ejecutándose; no es una pausa de depuración):

1. Confirmar visualmente que aparece el texto destino completo y no el texto anterior.
2. Confirmar que `dialogue_current` señala el ID destino y que el tracker está en el marcador de espera de ese bloque.
3. Solo con autorización expresa para avanzar el juego, enviar una única entrada normal del usuario (por ejemplo, Enter).
4. Confirmar que el intérprete avanza desde el diálogo destino al siguiente punto natural del script.
5. Comprobar que no vuelve al diálogo anterior, no salta un bloque y no entra en una escena incorrecta.

El criterio decisivo no es solo que aparezca el texto: el juego debe poder continuar desde el destino con su flujo normal.

## Fase D — Resultado

Marcar la prueba como **PASS** solo si se cumplen todas estas condiciones:

- [ ] el ID destino se resolvió dentro del script activo;
- [ ] el intérprete ejecutó el bloque desde su encabezado;
- [ ] el texto destino se observó en las celdas de texto;
- [ ] hubo una composición posterior que incluyó el plano de texto;
- [ ] el blit DirectDraw a la superficie primaria terminó correctamente;
- [ ] el tracker identificó el marcador `HA\\` del bloque destino;
- [ ] una entrada normal avanzó desde ese diálogo;
- [ ] la continuación fue coherente y no reutilizó el contexto de la página anterior.

Si falla cualquiera de los puntos, registrar el estado/error MCP y detener la prueba. El adaptador intenta reejecutar el bloque anterior y verificar que su texto vuelva a presentarse; no declarar rollback correcto solo porque ESI haya regresado.

## Estado de esta prueba

**Pendiente de ejecución runtime autorizada.** La compilación Release NT x64, las pruebas nativas del núcleo/cola/IPC, el parser PC-98, el tracker DOS y el observador de texto se validan por CI. Ninguna de esas pruebas sustituye esta comprobación real de presentación y avance del juego.
