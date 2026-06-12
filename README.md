# ucvsh - Intérprete de Comandos (Shell)

`ucvsh` es una implementación desde cero de una shell interactiva de tipo Unix, desarrollada en C. Este proyecto fue construido como parte de la asignatura de Sistemas Operativos en la Universidad Central de Venezuela (UCV).

La shell es capaz de interpretar y ejecutar comandos del sistema, gestionar tuberías (pipes), redirecciones de entrada/salida y administrar procesos en segundo plano (background).

## Características Principales
* **Ejecución de Comandos Externos:** Búsqueda dinámica de binarios en las rutas de la variable de entorno `$PATH`.
* **Comandos Internos (Built-ins):** * `cd`: Cambio de directorio.
* **Tuberías (Pipes):** Soporte para encadenar múltiples comandos mediante `|` , conectando la salida estándar de uno con la entrada del siguiente.
* **Procesos en Segundo Plano:** Ejecución asíncrona de comandos usando el operador `&`. Incluye una tabla de trabajos dinámica que limpia procesos huérfanos sin bloquear la terminal.
* **Redirección de E/S:** Soporte para redirigir la entrada (`<`) y salida (`>`, `>>`) a archivos.
* **Historial de Comandos:** Navegación por comandos anteriores (persistencia en archivo).


## Arquitectura del Sistema

El proyecto está modularizado en los siguientes componentes:

* `main.c`: Punto de entrada y ciclo de vida principal (REPL).
* `parser.c` / `parser.h`: Análisis léxico y sintáctico de la entrada del usuario. Transforma cadenas de texto en listas enlazadas.
* `executor.c` / `executor.h`: Motor de ejecución. Maneja la creación de procesos (`fork`), reemplazo de imágenes (`execv`), tuberías (`pipe`, `dup2`) y sincronización (`waitpid`).
* `jobs.c` / `jobs.h`: Estructura de datos y lógica para mantener el registro de procesos en *background* (Running/Done).
* `history.c`: Gestión de persistencia y memoria del historial de comandos.
* `input.c` / `signKeyboard.c`: Interacción directa con la terminal y manejo de señales del sistema.


### Requisitos Previos
* Entorno Linux (o WSL en Windows).
* Compilador GCC.
* GNU Make (opcional, si tienen un Makefile).

### Compilación
make