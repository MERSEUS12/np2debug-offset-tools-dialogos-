# NP2Debug Next — base de trabajo bloqueada

## Regla principal del proyecto

**La base obligatoria de NP2Debug Next es MERSEUS12/np2debug-offset-tools-dialogos-.**

Este repositorio es el que el usuario ya consiguió compilar. Todas las implementaciones futuras deben partir de esta base y preservar su compilabilidad.

- Rama de referencia original: master
- Rama de integración de NP2Debug Next: np2debug-next-integration
- Commit conocido de la base compilable: 4a74d1458b5ffcb3b1db2851391fd02f9a5a0e6e
- Compilación de referencia: Release NT x64, GitHub Actions run 41518365409
- El trabajo nuevo se hace en la rama de integración, nunca directamente sobre master.

## No cambiar de base

No sustituir esta base por NP2kai, NP2kai-v2 ni por el repositorio integrado. Esos proyectos son fuentes de módulos y referencias para comparar o portar funcionalidades, no el punto de partida.

No reconstruir el proyecto desde cero. No eliminar ni reemplazar funciones existentes para facilitar una integración.

## Proceso obligatorio de cada cambio

1. Partir de la última versión de np2debug-next-integration.
2. Inspeccionar las dependencias y las llamadas existentes antes de editar.
3. Incorporar un módulo pequeño y aislado.
4. Actualizar el proyecto Windows y sus archivos de configuración si el módulo requiere compilación.
5. Ejecutar el workflow Release NT x64.
6. Revisar el resultado y los logs; no declarar éxito hasta que el job termine correctamente.
7. Si falla, corregir o revertir el último cambio antes de continuar.
8. Mantener el inventario completo de herramientas acordadas; no dar una función por implementada hasta que exista código y prueba.

## Primera implementación: fachada interna

Se ha añadido:
- integration/np2dn_debug_api.h
- integration/np2dn_debug_api.c

La fachada expone una API estable sobre funciones ya presentes en break.c:
- estado del depurador y controles resume/pause/step/step-over;
- activar/desactivar breakpoints por segmento:offset o dirección lineal;
- armar/cancelar salto de diálogo;
- armar/cancelar redirección de escena;
- fast-forward y estado de avance;
- consulta del diálogo/posición actual.

La fachada se ha añadido a win9x/np21.vcxproj. El workflow .github/workflows/build-np21debug.yml ahora también se activa en la rama de integración.

### Restricción importante

La fachada es todavía una API in-process. Sus funciones deben llamarse desde el hilo de emulación/UI. No conectar todavía un servidor MCP externo directamente a estas funciones: primero se debe implementar una cola de comandos segura entre el MCP y el hilo del emulador.

Además, np2break_is_next() es un hook con efectos secundarios (actualiza seguimiento de escena, posición y fast-forward); nunca debe utilizarse como una simple consulta de estado. La fachada consulta el breakpoint de la instrucción actual sin ejecutar ese hook.

## Funciones que deben conservarse

La implementación debe preservar, como mínimo, las capacidades ya presentes en la base:
- breakpoints de ejecución, lectura y escritura;
- ejecución paso a paso y step-over;
- seguimiento de cambios de ESI y traza de salto;
- salto a diálogo con validación opcional de direcciones CS:IP;
- registro de apertura de archivos y redirección de scripts BIN;
- seguimiento de escenas y último script abierto;
- seguimiento de posición y extracción del diálogo actual;
- fast-forward por cantidad de diálogos, offset y modos de espera;
- pulsación automática de Enter y modo mash opcional;
- compatibilidad con el depurador y las ventanas existentes.

El inventario ampliado de NP2Debug Next (MCP, análisis de memoria, extracción/inyección, navegación, snapshots, automatización y pruebas) se integrará por módulos sin retirar las funciones anteriores.