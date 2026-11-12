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
6. no se detectó corrupción, bloqueo ni desincronización;
7. el adaptador confirmó que el flujo normal del juego está preparado para aceptar la siguiente acción ordinaria (avanzar, elegir, completar una espera o equivalente) sin reconstruir de nuevo el contexto anterior.

**Un cambio de registro, un nuevo offset o una lectura MCP diferente no son, por sí solos, evidencia de éxito.**

### 9.1 Contrato obligatorio de continuidad del núcleo (API v2)

El núcleo universal ahora exige que todo adaptador anuncie `NP2DN_DIALOGUE_CAP_VERIFY_STATE`, `NP2DN_DIALOGUE_CAP_VERIFY_UI` y `NP2DN_DIALOGUE_CAP_VERIFY_CONTINUITY`. La instantánea posterior debe marcar:

- `NP2DN_DIALOGUE_SNAPSHOT_STATE_VERIFIED`: el motor adoptó el destino y el adaptador comprobó su estado interno relevante.
- `NP2DN_DIALOGUE_SNAPSHOT_PRESENTED`: el diálogo seleccionado quedó activo y fue incluido en la composición de presentación que el adaptador puede demostrar.
- `NP2DN_DIALOGUE_SNAPSHOT_CONTINUATION_READY`: el estado del intérprete/motor es coherente y puede recibir la siguiente acción normal del juego desde el destino.

Si falta la verificación de estado o la señal de presentación, el salto falla como no verificado. Si ambas están confirmadas pero falta continuidad, el núcleo informa `NP2DN_DIALOGUE_ERROR_CONTINUITY_FAILED` y solicita rollback cuando el adaptador lo soporte. Ninguna de estas señales se puede sustituir por un cambio de puntero o registro.

El callback `prepare` es responsable de preparar el contexto necesario para entrar en el diálogo seleccionado (variables, flags, escena, decisiones y estado del intérprete que el juego requiera). El callback `verify` no puede marcar continuidad lista hasta confirmar el punto de espera/avance normal correspondiente al motor. El núcleo no inventa ni modifica esos datos: cada adaptador debe conocerlos.

La ampliación de continuidad subió `NP2DN_DIALOGUE_CORE_API_VERSION` a 2; el contrato de catálogo paginado lo eleva ahora a 3. El test del núcleo incluye un caso negativo: aunque el diálogo destino aparezca, si el adaptador no confirma continuidad, la operación no puede terminar en éxito y se solicita rollback.

### 9.2 Catálogo universal paginado (API v3)

El contrato incorpora `enumerate` como callback obligatorio del adaptador y una estructura común `NP2DN_DialogueCatalogEntry`. El adaptador entrega IDs estables por generación, escena, personaje, texto UTF-8 y locator opaco; cada motor/formato se encarga de convertir su codificación original a UTF-8.

El núcleo solicita páginas de 32 entradas, valida IDs únicos y cadenas terminadas, comprueba que la generación no cambie entre páginas y publica el nuevo catálogo solo cuando la lectura completa es coherente. La caché admite hasta 4096 entradas y texto de hasta 512 bytes por entrada. Si el adaptador informa más entradas, el núcleo conserva las primeras 4096 y marca explícitamente `catalog_truncated`; nunca afirma que la lista está completa.

La actualización del catálogo es independiente del salto: enumerar no modifica el juego. El locator sigue siendo opaco y solo el adaptador puede interpretarlo. Esto permite que el núcleo presente una biblioteca uniforme de diálogos sin asumir offsets, segmentos, punteros ni formatos de script propios de un motor.

### 9.3 Primer parser de catálogo: PC-98 con bloques 18 01 / 18 02

Se añadió un parser separado del núcleo universal para el formato PC-98 usado por el primer adaptador. Reconoce los marcadores `18 01` y `18 02` cuando están precedidos por `00`, `01` o `45`; busca el primer token de espera `\HA\` antes del siguiente bloque; devuelve el offset del carácter `H` como destino; conserva el ordinal de los bloques sin espera para que los IDs coincidan con el tracker actual; convierte `\CR` en salto de línea y conserva los demás bytes en su codificación original.

El parser recibe un búfer y una longitud explícitos, nunca lee fuera del rango suministrado y limita el texto a 512 bytes marcando truncamiento. Sus pruebas cubren IDs con bloques omitidos, destinos exactos, saltos de línea, bloques sin espera, marcadores falsos, límites de lectura y catálogos de salida truncados.

**Límite actual:** el parser está probado, pero todavía no está conectado a un adaptador runtime. El adaptador debe obtener del cargador del juego el segmento, inicio y longitud reales del script. No se debe escanear automáticamente todo el segmento de 64 KiB como si todo su contenido perteneciera al archivo: eso podría incluir datos residuales y producir destinos falsos.

### 9.4 Observación de carga de archivos DOS

Se añadió un tracker de solo lectura para las llamadas DOS INT 21h de apertura (`AH=3D`), lectura (`AH=3F`), posicionamiento (`AH=42`) y cierre (`AH=3E`). Correlaciona cada entrada con su retorno mediante el CS:IP de retorno, conserva el nombre/handle y reconstruye la relación entre offsets del archivo y el búfer de memoria.

El tracker solo expone un rango como script cargado cuando conoce el tamaño real (lectura EOF o seek al final), verifica que los bytes leídos cubran el archivo sin huecos y confirma que la correspondencia archivo→segmento sea contigua y quepa en el segmento. También admite un offset inicial distinto de cero. El reset del emulador limpia el historial del tracker.

La primera consulta de script reconoce nombres `.BIN` y `.M`; el tracker DOS subyacente no depende de una extensión concreta. La API `np2track_loaded_script_info` entrega segmento, offset inicial, longitud y nombre solo para rangos completos y reconocidos.

**Pendiente de validación runtime:** el observador y sus pruebas unitarias están integrados, pero aún no se ha comprobado con el juego ejecutándose que Macross cargue sus scripts mediante estas llamadas DOS y que el búfer leído coincida con el segmento ES usado por el intérprete. Hasta confirmar eso, el adaptador no debe declarar completo el catálogo.

### 9.5 Señal de presentación en la superficie host

La composición emulada por `scrndraw_draw` no se toma por sí sola como prueba de que el diálogo llegó a la ventana. Se añadió una segunda señal en `win9x/scrnmng.cpp`: después de cada `scrnmng_update`, el observador recibe el resultado real del `Blt` de DirectDraw hacia la superficie primaria. La ruta de recuperación por `DDERR_SURFACELOST` también conserva el HRESULT del segundo intento.

El observador enlaza esa confirmación con la generación de composición de texto más reciente. Solo incrementa `host_present_generation` cuando el blit devuelve `DD_OK` y la composición pendiente incluía el plano de texto; los blits gráficos, fallidos o repetidos de una misma composición no pueden confirmar el diálogo. Así, el adaptador podrá exigir estado del intérprete, composición con texto y blit host exitoso antes de marcar `PRESENTED`.



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


## 10. Estado de integración del núcleo y del adaptador PC-98

### 10.1 Cableado real

La rama `np2debug-next-integration` ya contiene el cableado de ejecución:

- `pccore_init()` inicializa la cola, registra el adaptador `pc98-ha-script` y arranca el servidor IPC.
- `np2dn_command_queue_pump()` ejecuta `np2dn_dialogue_tick()` en el hilo serializado de la interfaz/emulación. El núcleo no se ejecuta desde el hook por instrucción.
- `pccore_term()` cancela una operación pendiente y detiene el servidor IPC antes de destruir los subsistemas del emulador.
- `win9x/np21.vcxproj` incluye los módulos de núcleo universal, adaptador PC-98, catálogo de scripts, observador de texto, tracker DOS, cola, API de depuración e IPC.
- La cola y el servidor IPC aceptan los comandos nuevos 25–29; las consultas que llevan payload usan la ruta de respuesta binaria de hasta 256 bytes.

### 10.2 Herramientas MCP del catálogo y del salto universal

Se exponen seis herramientas nuevas, manteniendo las herramientas antiguas de salto de bajo nivel por compatibilidad:

- `dialogue_catalog_refresh`: reconstruye el catálogo activo y devuelve total, generación y truncamiento.
- `dialogue_catalog`: devuelve una página de entradas con IDs estables y texto UTF-8.
- `dialogue_catalog_entry`: obtiene una entrada concreta por índice.
- `dialogue_jump_request`: solicita el salto por ID; la aceptación de la solicitud no se presenta como éxito.
- `dialogue_jump_status`: expone fase, error, adaptador, IDs y evidencias de estado/presentación/continuidad.
- `dialogue_operation_cancel`: cancela la operación universal sin reemplazar `dialogue_jump_cancel`, que sigue controlando el salto antiguo.

El catálogo es de sesión y corresponde al script actualmente cargado. El núcleo mantiene IDs y localizadores opacos; no interpreta el formato PC-98.

### 10.3 Adaptador PC-98 inicial

El adaptador:

1. Obtiene el script cargado a partir del tracker de archivos DOS y exige que el archivo esté completamente mapeado en memoria.
2. Construye el catálogo con el parser PC-98 y genera IDs como `ARCHIVO#ordinal`.
3. Valida que el localizador siga perteneciendo a la generación actual, al mismo script/segmento, a un bloque válido `18 01` o `18 02`, y a un token de espera `HA\\` dentro de los límites del archivo.
4. Captura el segmento, ESI y contadores previos para poder restaurar el punto de espera anterior.
5. Aplica el salto usando el hook existente de `np2jump_arm`, sin ejecutar operaciones pesadas desde el hook por instrucción.
6. Verifica que el tracker vuelve a detectar el ESI y el bloque destino, que el texto destino aparece en las celdas observadas, que hubo una composición posterior con el plano de texto y que el blit DirectDraw hacia la superficie primaria terminó con `DD_OK`.
7. Si falla la verificación después de aplicar el salto, intenta volver al ESI anterior mediante el mismo mecanismo controlado.

La señal de presentación no es una estimación: `scrndraw_draw()` registra la composición efectiva y `scrnmng_update()` informa del resultado del blit host. El adaptador no acepta únicamente un cambio de registro o de ID.

### 10.4 Significado de «como un libro» y límites actuales

El objetivo funcional sigue siendo elegir una página/frase, entrar en un punto de espera normal del intérprete y poder continuar desde ahí con el flujo del juego. Para el primer adaptador PC-98, el destino se sitúa en el token `HA\\` asociado al bloque, no en un buffer de texto separado.

Esto **no convierte todavía el adaptador PC-98 en un sistema universal para cualquier juego**. Su alcance actual es el script PC-98 activo y el formato de marcadores reconocido. Tampoco reconstruye automáticamente variables narrativas, decisiones o escenas arbitrarias: el callback `prepare` conserva el estado de retorno y valida el contexto disponible; los motores que requieran reconstruir variables, flags, inventario, rutas o escenas deberán implementar esa preparación en su propio adaptador.

La aceptación final del comportamiento «libro» exige pruebas de ejecución controladas que confirmen, en orden: (a) aparece el texto destino en pantalla, (b) el intérprete queda en su espera normal, (c) una entrada normal del usuario avanza desde ese diálogo y (d) el juego sigue por la continuación esperada sin reutilizar accidentalmente el contexto de la página anterior. Esas pruebas de runtime no se han ejecutado en esta fase; no se debe declarar el sistema terminado ni ejecutar saltos/reanudar el emulador sin autorización expresa.

### 10.5 Verificación automatizada

En esta fase se actualizaron las pruebas de cola para comprobar los nuevos comandos de catálogo, solicitud, estado y cancelación; las pruebas IPC usan el comando 29 para verificar que el extremo superior del rango es aceptado. Las pruebas del núcleo universal, parser PC-98, tracker DOS y observador de texto continúan dentro del workflow de CI. La compilación completa Release NT x64 y el empaquetado del servidor MCP deben considerarse verificados únicamente cuando sus workflows correspondientes terminen con éxito en el commit actual.
