# Planificador Dieciochero — Sistemas Operativos

## Descripción

Programa desarrollado para la **Tarea 1 de Sistemas Operativos**, cuyo objetivo es ejecutar un conjunto de actividades representadas mediante un **grafo dirigido acíclico (DAG)**, respetando sus dependencias y un límite máximo de procesos concurrentes.

Cada actividad se ejecuta como un **proceso independiente**, utilizando `fork()` y comunicación mediante `pipe()`.

El programa recibe:

```text
./planificador plan.txt K
```

donde:

* `plan.txt` corresponde al archivo que contiene las actividades.
* `K` corresponde al número máximo de procesos que pueden ejecutarse simultáneamente.

---

## Compilación

El programa está escrito en **C17** y puede compilarse utilizando:

```bash
gcc -Wall -Wextra -std=c17 main.c -o planificador
```

No se utilizan threads ni la biblioteca `pthread` para crear hilos.

---

## Ejecución

La ejecución se realiza mediante:

```bash
./planificador plan.txt K
```

Por ejemplo:

```bash
./planificador plan.txt 2
```

En este caso, el planificador permite como máximo **2 procesos activos simultáneamente**.

---

## Formato del archivo de planificación

Cada línea del archivo `plan.txt` representa una actividad con el siguiente formato:

```text
ID_Actividad : Nombre_Actividad : tiempo_ms : [Dependencia1, Dependencia2, ...]
```

Por ejemplo:

```text
1 : prender_carbon : 500 :
2 : comprar_longaniza : 1200 :
3 : comprar_pan : 300 :
4 : asar_longaniza : 800 : 1, 2
5 : armar_choripan : 250 : 3, 4
6 : servir_choripan : 100 : 5
```

Una actividad sin dependencias puede dejar vacío el último campo.

Si no se especifica un tiempo de ejecución, el programa genera un tiempo aleatorio entre **100 y 5000 ms**.

---

## Funcionamiento

El programa realiza las siguientes etapas:

1. Lee y procesa el archivo de planificación.
2. Almacena las actividades y sus dependencias.
3. Verifica que los identificadores no estén duplicados.
4. Verifica que todas las dependencias correspondan a actividades existentes.
5. Comprueba que el grafo no contenga ciclos.
6. Ejecuta las actividades respetando sus dependencias.
7. Mantiene el límite máximo de procesos indicado por `K`.
8. Utiliza pipes para comunicar la finalización de las actividades a sus dependientes.
9. Maneja fallos internos y aborta únicamente las ramas que dependen de una actividad fallida.
10. Permite abortar la ejecución completa mediante `Ctrl+C` (`SIGINT`).
11. Muestra el estado final de todas las actividades.

---

## Arquitectura

Las actividades se ejecutan mediante **procesos**, no mediante threads.

El proceso principal actúa como planificador y crea procesos hijos mediante:

```c
fork()
```

Los procesos terminados son gestionados mediante:

```c
waitpid()
```

La comunicación entre procesos se realiza mediante:

```c
pipe()
```

El esquema general es:

```text
                 PROCESO PADRE
                      |
          +-----------+-----------+
          |           |           |
        fork()      fork()      fork()
          |           |           |
       Actividad 1 Actividad 2 Actividad 3
          |           |           |
          +----- pipes -----------+
                      |
                  Actividad 4
                      |
                  Actividad 5
                      |
                  Actividad 6
```

Una actividad dependiente espera recibir las notificaciones correspondientes a sus dependencias antes de comenzar su ejecución.

Por ejemplo, si:

```text
4 : asar_longaniza : 800 : 1, 2
```

la actividad 4 recibe mensajes como:

```text
TERMINADA:1
TERMINADA:2
```

y solamente después de recibir las notificaciones necesarias comienza su ejecución.

---

## Control de concurrencia

El parámetro `K` determina la cantidad máxima de procesos que pueden estar activos simultáneamente.

Por ejemplo:

```bash
./planificador plan.txt 2
```

permite como máximo dos procesos activos.

Cuando un proceso termina, el planificador puede crear otra actividad que esté lista para ejecutarse.

---

## Validación del DAG

Antes de comenzar la ejecución se realizan verificaciones sobre el grafo:

* Identificadores duplicados.
* Dependencias inexistentes.
* Presencia de ciclos.

Si se detecta un ciclo, el programa informa el error y no comienza la ejecución.

Ejemplo:

```text
Error: se detectó un ciclo que involucra la actividad 1.
```

---

## Comunicación mediante pipes

Los procesos utilizan pipes para recibir notificaciones relacionadas con la finalización de sus dependencias.

Los mensajes utilizados tienen un tamaño acotado y permiten identificar la actividad que produjo la notificación.

Ejemplos:

```text
TERMINADA:1
TERMINADA:2
FALLIDA:3
```

Esto permite que una actividad conozca el estado de sus dependencias antes de comenzar a ejecutarse.

---

## Manejo de errores

Si una actividad falla, las actividades que dependen de ella son abortadas.

Las actividades que no pertenecen a esa rama pueden continuar su ejecución.

Los estados utilizados por el programa son:

```text
PENDIENTE
EJECUTANDO
TERMINADA
ABORTADA
```

---

## Manejo de SIGINT

El programa captura la señal:

```text
SIGINT
```

que normalmente se genera al presionar:

```text
Ctrl+C
```

Cuando se recibe esta señal, el planificador aborta las actividades correspondientes y muestra el estado final.

Ejemplo:

```text
[PADRE] SIGINT recibido. Abortando todas las actividades.

--- Estado final ---
1 : TERMINADA
2 : TERMINADA
3 : TERMINADA
4 : ABORTADA
5 : ABORTADA
6 : ABORTADA
```

---

## Pruebas realizadas

Se realizaron pruebas para verificar:

* Lectura y procesamiento de archivos de planificación.
* Ejecución con distintos valores de `K`.
* Actividades con múltiples dependencias.
* Detección de dependencias inexistentes.
* Detección de ciclos.
* Comunicación mediante pipes.
* Manejo de fallos de actividades.
* Manejo de `SIGINT` mediante `Ctrl+C`.
* Ejecución de un escenario de estrés con **10.000 actividades**.

---

## Restricciones

El programa no utiliza threads para ejecutar las actividades.

La ejecución concurrente se implementa mediante procesos creados con:

```c
fork()
```

y sincronización mediante:

```c
waitpid()
```

La comunicación entre procesos se realiza mediante pipes.

---

## Archivos principales

```text
main.c
```

Contiene la implementación completa del planificador.

```text
plan.txt
```

Contiene un ejemplo de planificación con actividades y dependencias.

```text
README.md
```

Contiene la documentación del proyecto, instrucciones de compilación, ejecución y descripción del diseño.

