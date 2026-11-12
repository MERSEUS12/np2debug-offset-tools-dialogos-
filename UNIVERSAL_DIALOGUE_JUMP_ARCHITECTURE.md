# NP2Debug Next — Arquitectura del sistema universal de salto de diálogos

**Estado:** especificación de arquitectura inicial.  
**Alcance:** videojuegos de distintos motores, arquitecturas y formatos de guion. Macross/PC-98 es el primer adaptador de prueba, no el límite del sistema.

## 1. Objetivo

Permitir que una herramienta seleccione un diálogo y que NP2Debug Next lleve el juego hasta ese diálogo de forma controlada, actualizando el estado interno que realmente consume el motor y verificando que el diálogo de destino se presenta.

No se considera un salto correcto que únicamente cambie un registro, un offset, un ID detectado o el texto que devuelve una herramienta de depuración. El resultado debe quedar confirmado en el estado de ejecución del juego y en su presentación.

## 2. Principio de diseño

**El núcleo es común; la interpretación y transición son específicas del motor o del juego.**

No existe una única dirección universal que represente el diálogo actual en todos los videojuegos. Según el juego, el destino puede ser:

- un offset dentro de un script;
- una instrucción o estado de una máquina virtual;
- un ID de evento o nodo de diálogo;
- una entrada de una secuencia de escena;
- un objeto o estructura de estado administrada por el motor;
- una función o API interna del propio motor.

Por ello, el núcleo no debe asumir registros como ESI, marcadores como `\\HA`, segmentos x86 ni formatos `.BIN`.

## 3. Capas

### 3.1 Núcleo universal

Responsabilidades:

- mantener el catálogo de diálogos detectados;
- asignar y conservar identificadores estables dentro de una sesión;
- consultar el diálogo actual;
- recibir una selección de destino;
- solicitar al adaptador una transición segura;
- gestionar estados, tiempos de espera, cancelación y errores;
- exigir verificación antes de declarar el salto como completado.

El núcleo no interpreta directamente scripts ni escribe registros específicos de un juego.

### 3.2 Adaptadores de motor o juego

Cada adaptador conoce cómo funciona una familia de juegos o un título concreto. Debe declarar sus capacidades y no fingir que soporta operaciones que no puede verificar.

Contrato conceptual:

```c
typedef struct {
    const char *adapter_id;
    unsigned int api_version;
    unsigned int capabilities;
} DialogueAdapterInfo;

/* Todas las funciones operan sobre el estado del juego en el hilo seguro
   definido por el adaptador. Los tipos concretos se definirán al implementar. */

int dialogue_adapter_observe(/* context, current dialogue, scene */);
int dialogue_adapter_enumerate(/* context, dialogue catalog */);
int dialogue_adapter_resolve(/* target id, target locator */);
int dialogue_adapter_validate(/* target locator, preconditions */);
int dialogue_adapter_prepare(/* target locator, transition checkpoint */);
int dialogue_adapter_apply(/* prepared transition */);
int dialogue_adapter_verify(/* expected target, runtime and presentation */);
int dialogue_adapter_rollback(/* checkpoint, if rollback is supported */);
```

La interfaz es un contrato de diseño, no código compilable ni una API ya implementada.

### 3.3 Puente del depurador

Conecta el núcleo con el proceso del juego mediante los mecanismos disponibles: API del motor, eventos de ejecución, puntos de interrupción, memoria, scripts o IPC.

Debe respetar el hilo de ejecución y el modelo de memoria del emulador. El núcleo universal no debe acceder directamente a variables internas de un adaptador.

### 3.4 Verificación de presentación

Comprueba que el destino no solo fue seleccionado, sino que llegó al sistema de presentación.

Se pueden combinar, según las capacidades del adaptador:

- estado interno de diálogo activo;
- identificador o puntero consumido por la rutina de presentación;
- evento de actualización/renderizado;
- contenido del búfer o superficie de texto;
- captura de pantalla y comparación del área de diálogo;
- OCR como apoyo, nunca como única prueba cuando existan señales internas fiables.

Si no existe una señal suficiente, el resultado debe ser **no verificado**, no éxito.

## 4. Modelo común de diálogo

Cada entrada del catálogo debe poder representar, cuando la información exista:

- `dialogue_id`: identificador estable de sesión;
- `engine_id` y `adapter_id`;
- `scene_id` o contexto de escena;
- `script_id` o recurso de origen;
- `speaker` y `text`, si se pueden leer;
- `source_locator`: ubicación donde se almacena el diálogo;
- `execution_locator`: estado o punto de ejecución que lo activa;
- `presentation_state`: desconocido, pendiente, visible o no confirmado;
- `confidence`: fiabilidad de la detección.

Los localizadores deben ser tipados. Un offset de archivo no se debe confundir con una dirección virtual, un puntero de memoria o un ID de evento.

## 5. Flujo de un salto

El núcleo gestionará la operación como una transacción con estados explícitos:

1. **OBSERVE** — obtener escena, diálogo actual y estado de ejecución.
2. **RESOLVE** — resolver el ID seleccionado a un localizador específico del adaptador.
3. **VALIDATE** — comprobar que el destino existe y que el estado actual permite la transición.
4. **QUIESCE** — alcanzar un punto seguro sin dejar el juego en una espera o estado inconsistente.
5. **PREPARE** — guardar el contexto necesario para recuperación y preparar los cambios.
6. **APPLY** — ejecutar la transición mediante el adaptador.
7. **RESUME** — permitir que el motor procese el nuevo estado cuando corresponda.
8. **VERIFY** — comprobar que el motor adoptó el destino y que la presentación lo mostró.
9. **COMMIT** — declarar éxito solo tras la verificación.
10. **ROLLBACK / FAILED** — recuperar el estado anterior si es seguro; en caso contrario, detenerse e informar qué quedó pendiente.

No todos los motores necesitarán todos los pasos de la misma manera, pero el resultado y los estados deben ser coherentes para el usuario.

## 6. Seguridad y consistencia

- No cambiar registros de CPU a ciegas.
- No asumir que el puntero de lectura del script es el estado que controla el diálogo visible.
- No escribir memoria sin conocer estructura, tamaño, propietario y momento seguro.
- Validar los valores anteriores y posteriores a cada cambio.
- Mantener los cambios de un salto acotados y trazables.
- No reanudar automáticamente si la operación falla o queda sin verificar.
- Permitir cancelar una operación armada.
- Registrar localizador, estado previo, cambios realizados, punto de ejecución y resultado de verificación.
- Si el adaptador no puede garantizar una transición segura, debe rechazarla en lugar de improvisar.

## 7. Adaptador inicial: Macross / PC-98

El código existente se conserva como primer adaptador experimental.

En la rama actual, `break.c` implementa un mecanismo específico:

- `np2track_step()` detecta el patrón `HA\\` cuando `ES` coincide con el segmento del script.
- `np2track_current()` reconstruye el bloque anterior y devuelve el texto aproximado.
- `np2jump_arm()` arma un destino basado en segmento y ESI.
- `np2break_is_next()` espera el marcador y, cuando coincide, asigna el valor de destino a `CPU_ESI`.

Este mecanismo demuestra una forma de localizar un punto de espera e intervenir en el intérprete. **No demuestra por sí solo que el motor haya actualizado el estado de diálogo ni que la pantalla haya presentado el destino.** La prueba visual pendiente sigue siendo un requisito de aceptación del adaptador Macross.

El adaptador inicial deberá encapsular esas particularidades sin filtrarlas al núcleo universal.

### 7.1 Auditoría de presentación PC-98 (hallazgos del código fuente)

La ruta de texto del emulador se encuentra en `pccore.c:drawscreen()` y `vram/maketext.c`:

- La VRAM de texto que consume el renderizador está en `mem[0xA0000 + cell*2]`; el atributo de cada celda está en `mem[0xA2000 + cell*2]`.
- El renderizador solo considera celdas activas dentro de su recorrido de pantalla. En modo de 80 columnas incrementa el índice de celda de uno en uno; en modo de 40 columnas lo incrementa de dos en dos.
- El inicio de la secuencia de celdas procede de `GDC_SCROLL`; el avance de fila utiliza `GDC_PITCH`. También intervienen los registros de sincronización, el desplazamiento vertical, los intervalos de espera y los cambios de página de scroll. Por ello, escanear todo el rango de VRAM en busca del texto esperado no demuestra que esté en pantalla.
- `maketext()` y `maketext40()` convierten los códigos de carácter y atributos en píxeles dentro de `np2_tram`. `scrndraw_draw()` mezcla esa superficie con los planos gráficos según el modo de pantalla y actualiza la superficie nativa bloqueada por `scrnmng_surflock()`.

**Precaución importante sobre `drawcount`:** el contador se incrementa en `drawscreen()` después de llamar a `scrndraw_draw()`, pero el incremento no está condicionado a que esta última haya obtenido una superficie válida ni a que haya ejecutado una función de dibujo. `scrndraw_draw()` puede salir por superficie nula o tabla de dibujo nula y devolver igualmente el control al llamador. Por tanto, `drawcount` aislado no es evidencia suficiente de que los píxeles hayan sido actualizados.

La API de depuración actual (`np2dn_memory_read()` y `np2dn_memory_read_block()`) permite lecturas físicas mediante `memp_read8()` y lecturas lógicas mediante `meml_reads()`, pero no expone todavía una instantánea semántica de las celdas que el renderizador considera visibles, ni confirma que la superficie nativa se haya dibujado correctamente.

#### Consecuencia para el adaptador

La verificación deberá basarse en una observación del propio recorrido activo del renderizador, no en un escaneo indiscriminado de VRAM. La instrumentación más fiable sería registrar, durante una actualización de texto realmente procesada, los índices de celda y bytes de carácter/atributo que se consumieron, y asociar ese registro a una generación de dibujo que solo avance cuando se haya obtenido la superficie y ejecutado el dibujo. Además, deberá comprobarse que el plano de texto está habilitado y considerar la superposición de planos gráficos.

Esto aún no prueba el repintado final de la ventana de Windows. Si el criterio de aceptación exige confirmar lo que ve el usuario, habrá que añadir una señal del host o una captura de la superficie presentada. La distinción entre RAM de texto, superficie emulada y ventana del host debe permanecer explícita en el contrato de verificación.

## 8. Plan de implementación

### Fase A — Contrato y separación
- Definir los tipos comunes, capacidades y estados de operación.
- Separar el flujo de salto del conocimiento específico de Macross.
- Mantener compatibilidad con las herramientas MCP actuales.

### Fase B — Adaptador Macross
- Encapsular detección del marcador, segmento, offsets y punto seguro.
- Investigar y localizar el estado real que consume la rutina de presentación.
- Implementar transición y verificación de pantalla/estado.
- No marcarlo como completado hasta que el diálogo de destino sea visible.

### Fase C — Segundo adaptador
- Elegir una familia de motor distinta para comprobar que el núcleo no depende de ESI, x86, `\\HA` ni scripts BIN.
- Mantener las funciones específicas dentro de ese adaptador.

### Fase D — Catálogo y herramientas
- Exponer descubrimiento, búsqueda, selección, salto, cancelación y resultado verificado mediante MCP.
- Mostrar claramente las capacidades de cada adaptador y los saltos no verificables.

## 9. Criterios de aceptación universales

Un adaptador solo declara un salto exitoso cuando:

1. el destino solicitado se resolvió sin ambigüedad;
2. el juego aceptó el nuevo estado;
3. el motor avanzó por su flujo normal de procesamiento;
4. el diálogo de destino quedó activo;
5. existe evidencia de que fue presentado;
6. no se detectó corrupción, bloqueo ni desincronización.

**Un cambio de registro, un nuevo offset o una lectura MCP diferente no son, por sí solos, evidencia de éxito.**

---

Esta especificación define la arquitectura objetivo. No afirma que el núcleo universal, el catálogo, los adaptadores ni la verificación de presentación ya estén implementados.

#### 7.2 Semántica real del retorno de `scrndraw_draw()`

La inspección completa de `vram/scrndraw.c` añade una precisión importante a la instrumentación:

- `scrndraw_draw()` inicializa `ret = 0`. Si `scrnmng_surflock()` devuelve `NULL`, sale sin dibujar; si `sdraw_getproctbl()` devuelve `NULL`, también sale sin dibujar.
- Cuando la superficie y la tabla de funciones son válidas, configura origen/destino y ejecuta la función de dibujo seleccionada mediante `(*(*sdrawfn))(&sdraw, height)`. En la ruta normal esa función es `void` y `ret` permanece en cero.
- `ret` solo recibe un valor de retorno en la rama especial que llama a `rasterdraw()`. Por tanto, **el valor devuelto por `scrndraw_draw()` no es un indicador general de éxito**: cero puede significar tanto salida temprana como dibujo normal ejecutado.
- `pccore.c` asigna el retorno a `pcstat.screenupdate` y luego incrementa `drawcount`; ninguno de esos valores, aislado, permite distinguir una salida temprana de una ejecución normal del dibujador.

Por ello, no se debe implementar la generación de superficie como `if (scrndraw_draw()) generation++`. Si se añade instrumentación, debe registrar explícitamente el camino de ejecución: superficie adquirida, tabla de dibujo válida, plano de texto incluido en la selección y función de dibujo invocada. Esa señal debe nombrarse como **dibujo emulado ejecutado**, no como confirmación de que la ventana host ya presentó los píxeles. La ruta rasterizada debe registrar además su resultado específico sin confundirlo con el indicador común.

La observación de celdas también necesita distinguir dos cosas: las celdas que `maketext()` recalculó en una llamada y el contenido completo que permanece en `np2_tram`. Como el renderizador puede omitir filas/celdas sin renovación y conservar píxeles previos, una lista de celdas procesadas en la última llamada no representa necesariamente toda la superficie visible. Una futura instantánea semántica tendrá que ser persistente y mantener coherencia con invalidaciones, cambios de modo, scroll, página, atributos y redibujados completos; alternativamente, una lectura de observación puede reproducir el recorrido activo sin modificar estado.



#### 7.3 Contrato concreto para observar texto y dibujo ejecutado

La revisión de las rutas completas obliga a ampliar la propuesta anterior: no basta con instrumentar únicamente `maketext()` y `maketext40()`.

- En el camino de texto independiente, `drawscreen()` llama a una de esas dos funciones.
- Cuando texto y gráficos se componen juntos, puede utilizar `maketextgrph()` o `maketextgrph40()`. Una instrumentación limitada a las dos primeras funciones dejaría fuera precisamente los modos mixtos.
- `scrndraw_draw()` decide qué plano compone mediante `bit` y el selector `bit & 7`. Los casos 4, 5 y 6 incorporan `np2_tram` como texto solo o combinado con un plano gráfico. Por ello, la señal debe registrar el plano realmente seleccionado, no inferirlo solo de que `gdcs.textdisp` tenga el bit ENABLE.
- La adquisición de superficie y la obtención de la tabla de dibujo ocurren antes de invocar la función. La ruta normal invoca una función `void`; la ruta rasterizada devuelve un resultado propio. Conviene separar «función de composición invocada» de «resultado rasterizado», sin convertir ninguno de ellos en una afirmación sobre el repintado de Windows.

**Contrato recomendado, en dos señales independientes:**

1. **Snapshot semántico de texto activo:** mantener una caché persistente de celdas que fueron consumidas por las rutas de composición de texto, con índice de celda, bytes de carácter y atributo, modo 40/80 columnas y generación de configuración de pantalla. Debe cubrir tanto `maketext*` como `maketextgrph*`. Se invalida ante reinicialización, borrado/reconstrucción completa de la superficie de texto y cambios que alteren el recorrido (modo, scroll/página, pitch o sincronización vertical). Las actualizaciones parciales sustituyen solo las celdas realmente procesadas; no borran el resto de la caché. La lectura de esta caché será de solo lectura y no ejecutará el renderizador.
2. **Registro de composición de superficie:** en `scrndraw_draw()`, publicar un resultado estructurado por intento: superficie adquirida, tabla válida, plano(s) seleccionados, función normal invocada o rasterizador invocado, y contador monotónico de composiciones ejecutadas. El contador solo avanza después de la invocación efectiva. Un intento que sale antes queda registrado como no ejecutado. El nombre y contrato deben indicar «composición emulada», nunca «ventana presentada».

La observación del adaptador PC-98 combinará ambas señales: comprobará que las celdas del texto objetivo pertenecen al recorrido/caché activa y que una composición posterior incluyó el plano de texto. Si hay composición mixta, deberá conservarse el dato de que el plano gráfico puede cubrir o alterar parte del texto. Para afirmar que el usuario lo vio, seguirá haciendo falta una tercera evidencia del host (por ejemplo, captura de la superficie/ventana); las dos señales anteriores solo prueban el estado interno de composición del emulador.

**Decisión de implementación:** no reproducir desde una API de consulta el algoritmo de scroll/CRT ni llamar a `maketext*` para «observar», porque esas funciones mutan `np2_tram`, `renewal_line`, `tramupdate` y estado de blink/cursor. La instrumentación debe acompañar el recorrido real que ya ejecuta el emulador y mantener una caché incremental invalidable. Antes de codificarla habrá que localizar y revisar las implementaciones completas de `maketextgrph()` y `maketextgrph40()`, además de todos los puntos que reinicializan o borran `np2_tram`.


#### 7.4 Puntos reales de actualización e invalidación de la caché

La revisión de `vram/maketgrp.c` confirma que las dos rutas mixtas existen por separado: `maketextgrph()` (80 columnas) y `maketextgrph40()` (40 columnas). Ambas recorren el plano de texto con sus propios parámetros GDC y CRT, además del plano gráfico. Por tanto, el registro de celdas debe instrumentarse dentro de las rutas que realmente leen los bytes de texto, no deducirse después a partir de los píxeles mezclados.

Los puntos de borrado encontrados para `np2_tram` son:

- `scrndraw_initialize()`: borra toda la superficie `np2_tram`. Debe invalidar la caché completa.
- `dispsync_renewalvertical()`: borra las bandas superiores e inferiores cuando cambia la geometría vertical (`text_vbp`, `textymax` y `scrnymax`). Debe invalidar las filas correspondientes a esas bandas y avanzar la generación de geometría; no hace falta descartar celdas de filas que no fueron borradas.
- `maketext()`, `maketext40()` y las dos funciones mixtas actualizan regiones de la superficie durante el recorrido normal. Sus renovaciones parciales deben actualizar la caché de forma incremental.

La caché no puede asumir que cada llamada reconstruye todo el plano: las funciones pueden omitir líneas o regiones que no requieren renovación. Tampoco debe marcar como válido un bloque de texto solo porque sus bytes sigan presentes en la RAM de texto; el registro debe indicar que esos bytes fueron consumidos por una ruta de composición vigente bajo la configuración de pantalla actual.

**Siguiente tarea de código:** revisar los bucles exactos de lectura de texto en ambas funciones de `maketgrp.c`, definir un pequeño tipo interno de observación sin dependencias del núcleo universal y añadir las actualizaciones/invalidation hooks en los cuatro caminos de composición y los dos puntos de borrado identificados. La API MCP y el núcleo universal consumirán después una copia estable de esa observación; no accederán directamente a variables locales del renderizador.


#### 7.5 Corrección: semántica distinta entre texto independiente y composición mixta

La inspección de los bucles completos de `maketext()`, `maketext40()`, `maketextgrph()` y `maketextgrph40()` corrige una suposición de las propuestas de instrumentación anteriores:

- `maketext()` y `maketext40()` sí consumen los códigos de carácter de `mem[0xA0000 + cell*2]`, el segundo byte usado para determinar el modo/código de carácter, y los atributos de `mem[0xA2000 + cell*2]`. A partir de ellos resuelven la fuente, cursor, blink, inversión, subrayado y otros efectos, y generan píxeles en `np2_tram`. Estas rutas permiten una observación semántica por celda, siempre que se registre el contenido realmente leído y se tenga en cuenta que una celda puede ocupar más de una posición (por ejemplo, caracteres kanji de dos celdas).
- `maketextgrph()` y `maketextgrph40()` no leen los bytes de carácter de `0xA0000`. Leen los atributos de color de `0xA2000` para cada posición de texto y usan máscaras de bits procedentes del plano gráfico (`GRPHDATASET`, `pal_monotable`) para decidir dónde aplicar esos colores sobre `np2_vram`. En esta ruta mixta, la forma de los glifos/píxeles procede del plano gráfico; no se puede afirmar qué carácter se presentó observando solamente los atributos de la TRAM.
- La ruta mixta tiene por ello un contrato de observación distinto: registrar las posiciones y atributos TRAM consumidos, la página/plano gráfico y el intervalo de filas/píxeles realmente compuesto. Esto demuestra que se aplicó la capa de color de texto a una máscara gráfica, pero **no identifica semánticamente el texto**. Para identificar el diálogo mostrado en ese modo se necesita otra evidencia: estado interno del juego, una decodificación fiable de la máscara gráfica o captura/OCR de la superficie compuesta, según el adaptador.
- En `scrndraw_draw()`, los modos 4, 5 y 6 seleccionan texto solo o texto combinado con el plano gráfico 1 o 2. La evidencia debe registrar el selector real y el origen gráfico, no inferir que el texto semántico fue compuesto porque el bit de texto esté habilitado.

**Contrato revisado de observación:**

1. **Ruta de caracteres (texto independiente):** snapshot incremental de celdas con dirección TRAM, bytes de carácter, atributo, modo 40/80 columnas, interpretación de pares/kanji y generación de geometría. Registrar únicamente las celdas que el bucle de `maketext*` haya leído durante una renovación efectiva; conservar las celdas no renovadas y aplicar las invalidaciones documentadas.
2. **Ruta mixta (máscara gráfica + atributos TRAM):** registro separado de atributos consumidos, coordenadas/fila, plano gráfico y máscara de píxeles compuesta. No rellenar el campo de carácter con un valor inferido ni reutilizar el snapshot semántico de la ruta independiente.
3. **Composición emulada:** en `scrndraw_draw()`, registrar adquisición de superficie, tabla válida, selector de planos, función de composición invocada y generación monotónica posterior a la invocación efectiva. La ruta rasterizada debe registrar su resultado específico. Esto no confirma por sí solo que Windows haya presentado la ventana.
4. **Verificación del diálogo:** el adaptador debe elegir la evidencia según la ruta activa. En texto independiente puede cotejar los códigos/atributos activos y la composición posterior; en ruta mixta debe añadir evidencia semántica externa al registro TRAM de atributos. Si la evidencia disponible no identifica el texto destino, el resultado será `unverified`, nunca éxito.

Esta corrección sustituye cualquier lectura de las secciones 7.3 y 7.4 que implique que las cuatro funciones leen códigos de carácter o que una única caché semántica de celdas basta para los cuatro caminos. Las cuatro rutas siguen siendo puntos de instrumentación, pero no proporcionan el mismo tipo de evidencia.


#### 7.6 Auditoría de escrituras TRAM y discrepancia de flags de renovación

La inspección de `mem/memtram.c`, `io/gdc.h` y los consumidores de `tramupdate` revela una discrepancia que debe resolverse antes de basar la verificación en renovaciones incrementales:

- `memtram_wr8()` y `memtram_wr16()` marcan las celdas afectadas mediante `tramupdate[index] = 1` y activan `gdcs.textdisp |= 1`. Los accesos a la zona de caracteres y a la zona de atributos no usan el mismo filtro de escritura, y los accesos de palabra pueden marcar dos índices o envolver el índice 0xFFF/0.
- `maketext()` y `maketext40()` consideran cualquier valor distinto de cero en `tramupdate[edi]`, y al terminar limpian toda la matriz `tramupdate`.
- En cambio, `maketextgrph()` y `maketextgrph40()` buscan específicamente `tramupdate[edi] & 2`. En los caminos de escritura TRAM revisados, las funciones de escritura asignan el valor 1, no el bit 2.
- `GDCSCRN_ALLDRAW` es 0x04, mientras que la marca ordinaria de renovación de texto activada por las escrituras es 0x01. Por tanto, una renovación mixta puede entrar por `gdcs.textdisp` y aun así no marcar `new_flag` mediante el filtro incremental descrito si no existe otra causa de renovación completa o gráfica.

**Conclusión:** hay una posible incompatibilidad entre el productor de dirty flags TRAM y el consumidor de la ruta mixta. No se debe cambiar silenciosamente el filtro `& 2` ni asumir que es un bug confirmado: antes hay que localizar todos los productores de `tramupdate`, comprender el significado histórico de cada bit y comprobar si existe algún camino de actualización alternativo que justifique el bit 2.

**Regla para el observador:** no reutilizar `tramupdate` como única fuente de verdad. La observación de texto independiente debe registrar las lecturas reales del renderizador. La observación mixta debe registrar los atributos realmente consumidos y las filas/píxeles que se regeneraron; además, debe informar de cualquier inconsistencia entre una escritura TRAM y la renovación mixta resultante. Cualquier corrección del dirty tracking debe tratarse como un cambio funcional separado, con pruebas de regresión para ambos modos y sin alterar la semántica del renderizador por conveniencia del depurador.


**Aclaración del flujo completo de `drawscreen()`:** la ruta mixta no sustituye al renderizador de caracteres. Cuando se entra en el bloque de composición mixta, la condición exterior exige que `gdcs.textdisp & GDCSCRN_ENABLE` esté activo. Más abajo, en la misma llamada a `drawscreen()`, el bloque independiente de texto vuelve a comprobar esa habilitación y ejecuta `maketext()` o `maketext40()` cuando hay renovación pendiente. Después se llama a `scrndraw_draw()`, cuyo selector puede combinar el plano gráfico con `np2_tram` (casos 5 y 6).

Por tanto, para el diálogo semántico en este modo, la fuente primaria sigue siendo el registro de lecturas de carácter/atributo de `maketext*`; la ruta mixta añade una composición de color/máscara sobre el plano gráfico. La captura/OCR no es obligatoria si la pasada independiente de caracteres está habilitada y su composición posterior se confirma. Sí puede ser necesaria como evidencia adicional cuando se quiera demostrar el aspecto final de la ventana host o cuando el adaptador no pueda obtener una señal semántica interna suficiente.

Esto también acota la discrepancia de la sección 7.6: el filtro `tramupdate & 2` puede afectar a la renovación incremental de la capa mixta, pero la pasada posterior `maketext*` reconoce cualquier flag distinto de cero y actualiza el plano de caracteres antes de limpiar `tramupdate`. La posible consecuencia es una capa mixta de color/máscara desactualizada, no necesariamente la ausencia del carácter en el plano de texto independiente. Se mantiene pendiente localizar todos los productores del bit 2 y comprobar el comportamiento visual antes de corregirlo.
