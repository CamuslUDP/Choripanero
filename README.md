Tarea 1 Sistemas Operativos: Planificador Dieciochero

Universidad Diego Portales - Escuela de Informática y Telecomunicaciones
Integrantes: Diego Briones, Matias Rojas Lara

Simulador y planificador de actividades modeladas como un Grafo Acíclico Dirigido (DAG). Cada actividad se ejecuta como un proceso independiente, las dependencias se notifican mediante pipes y el número de procesos simultáneos está limitado por un parámetro K. El programa tolera fallos de actividades individuales y reacciona a SIGINT (Ctrl+C).

Contenido del repositorio:
main.c: corresponde a la implementación completa del planificador.
plan.txt: corresponde al plan de ejemplo.
README.md: este documento, correspondiente a la explicación sobre las funciones implementadas, su modo de uso y la justificación de las decisiones de diseño tomadas.
.gitignore: ignora binarios y archivos de respaldo.

Compilación

Es necesario utilizar el siguiente comando:

gcc -Wall -Wextra -std=c17 main.c -o planificador -lpthread

El programa no usa hilos ni mecanismos de sincronización de hilos. El flag -lpthread se incluye solo porque la rúbrica pide ese comando de compilación.

Modo de uso

./planificador plan.txt K

plan.txt es el archivo previamente mencionado, el cual contiene las actividades. K corresponde a la cantidad de procesos que pueden estar creados al mismo tiempo, debe ser un entero mayor a cero.

Por ejemplo: ./planificador plan.txt 4, esto permitiría tener como máximo 4 procesos simultáneos.

Para probar el aislamiento de errores se puede forzar que una actividad falle con la variable de entorno PLANIFICADOR_FALLA, indicando el ID de la actividad.

Por ejemplo: PLANIFICADOR_FALLA=4 ./planificador plan.txt 4

Para simular la inspección de la Seremi basta con presionar Ctrl+C mientras se ejecuta.

Formato de plan.txt

Cada línea del archivo representa una actividad con el formato ID_Actividad : Nombre_Actividad : tiempo_ms : [Dependencia1, Dependencia2, ...]

Por ejemplo, el plan.txt del repositorio es:

1 : prender_carbon : :
2 : comprar_longaniza : 1200 :
3 : comprar_pan : 300 :
4 : asar_longaniza : 800 : 1, 2
5 : armar_choripan : 250 : 3, 4
6 : servir_choripan : 100 : 5

Si el tiempo está vacío (como en la actividad 1), se asigna uno aleatorio entre 100 y 5000 ms. Una actividad sin dependencias deja ese último campo vacío.

Funciones implementadas

Las funciones parsear_linea y cargar_plan leen el archivo plan.txt y guardan cada actividad (ID, nombre, tiempo, dependencias y estado) en un arreglo. La función buscar_actividad entrega el índice de una actividad a partir de su ID.

La función validar_dag revisa que no haya IDs duplicados, dependencias que no existen ni ciclos. Si algo falla, no se crea ningún proceso.

La función crear_proceso crea el pipe de la actividad y hace fork(). El hijo espera el mensaje de cada una de sus dependencias, simula el trabajo con nanosleep y avisa al padre cuando termina. Las funciones enviar_mensaje_fd y recibir_mensaje escriben y leen mensajes en un pipe, con un tamaño máximo de 100 bytes.

Las funciones propagar_dependencia y enviar_dependencias_ya_terminadas avisan a las actividades dependientes que una dependencia terminó (TERMINADA:id) o falló (FALLIDA:id). La función procesar_completadas hace que el padre lea los resultados de los hijos y actualice los estados. La función abortar_rama marca como abortada una actividad y todas las que dependen de ella.

La función manejar_sigint activa una bandera cuando se presiona Ctrl+C, mostrar_estados imprime el estado final de todas las actividades, y main valida los argumentos, carga y valida el plan, ejecuta el ciclo de planificación, recoge los procesos con waitpid y muestra el resultado.

Decisiones de diseño

Se usan procesos y no hilos porque el enunciado prohíbe los hilos, así que cada actividad es un proceso creado con fork(). Además, si uno falla, el resto del simulador no se ve afectado.

Cada actividad tiene su propio pipe de entrada, donde el padre le escribe las notificaciones de sus dependencias. Además hay un pipe central donde todos los hijos le avisan su resultado al padre.

Para controlar K, antes de crear un proceso se cuentan los que están en ejecución y solo se crea otro si son menos que K. Un hijo puede crearse mientras sus dependencias aún corren, pero queda bloqueado esperando sus mensajes y no empieza a trabajar hasta recibirlos todos. Ese proceso en espera también cuenta dentro de K.

No hay busy waiting porque el padre y los hijos esperan bloqueados en read(), en vez de dar vueltas en un ciclo. Tampoco hay race conditions porque no hay memoria compartida: solo el padre modifica los estados y los hijos se comunican únicamente por pipes.

Para el aislamiento de errores, si una actividad falla, las que dependen de ella reciben FALLIDA:id y quedan abortadas. Las demás ramas siguen normal.

Para SIGINT, el manejador solo levanta una bandera. El ciclo principal la revisa, aborta las actividades pendientes, envía SIGTERM a las que están corriendo y las recoge con waitpid para no dejar procesos zombie.

El DAG se valida antes de crear procesos para no dejar ninguno esperando algo que nunca va a pasar.

Pruebas realizadas

Con el plan de ejemplo y K=2 terminan las 6 actividades respetando sus dependencias. Con PLANIFICADOR_FALLA=4 y K=3, la actividad 4 falla, la 5 y la 6 quedan abortadas, y la 1, 2 y 3 terminan normal. Al presionar Ctrl+C durante la ejecución se aborta todo y se muestra el estado final. Con una dependencia inexistente, un ID duplicado o un ciclo, el programa informa el error y no crea procesos. Con 10.000 actividades sin dependencias y K=100 terminan todas, en unos 13 segundos.

Limitaciones

Con planes muy grandes que tienen muchas dependencias el programa se vuelve lento, porque buscar actividades y recorrer la lista en cada vuelta cuesta tiempo lineal. En una cadena de actividades (cada una depende de la anterior), 1.000 actividades tomaron unos 2 segundos y 2.000 unos 11 segundos, y con 4.000 o más no alcanzó a terminar en nuestras pruebas. Como mejora se podría usar una tabla hash de ID a índice y una lista de dependientes precalculada.