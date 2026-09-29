#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define MAX_ACTIVIDADES 10000
#define MAX_NOMBRE 100
#define MAX_DEPENDENCIAS 100
#define MAX_ID 20
#define TAM_MENSAJE 100

typedef enum {
    PENDIENTE,
    EJECUTANDO,
    TERMINADA,
    ABORTADA
} Estado;

typedef struct {
    char id[MAX_ID];
    char nombre[MAX_NOMBRE];
    int tiempo;
    char dependencias[MAX_DEPENDENCIAS][MAX_ID];
    int num_dependencias;
    Estado estado;
    pid_t pid;
    int pipe_entrada[2];
} Actividad;

static volatile sig_atomic_t interrupcion = 0;
static int pipe_completadas[2] = {-1, -1};
static Actividad actividades[MAX_ACTIVIDADES];
static int cantidad = 0;

static void manejar_sigint(int sig) {
    (void)sig;
    interrupcion = 1;
}

static int buscar_actividad(const char *id) {
    for (int i = 0; i < cantidad; i++) {
        if (strcmp(actividades[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *nombre_estado(Estado estado) {
    switch (estado) {
        case PENDIENTE: return "PENDIENTE";
        case EJECUTANDO: return "EJECUTANDO";
        case TERMINADA: return "TERMINADA";
        case ABORTADA: return "ABORTADA";
    }
    return "DESCONOCIDO";
}

static void limpiar_texto(char *texto) {
    size_t len = strlen(texto);
    while (len > 0 && (texto[len - 1] == '\n' || texto[len - 1] == '\r' || texto[len - 1] == ' ' || texto[len - 1] == '\t')) {
        texto[--len] = '\0';
    }
}

static int parsear_linea(char *linea, Actividad *a) {
    char *parte;
    char *resto;

    limpiar_texto(linea);
    if (linea[0] == '\0' || linea[0] == '#') {
        return 0;
    }

    memset(a, 0, sizeof(*a));
    a->estado = PENDIENTE;
    a->pid = -1;
    a->pipe_entrada[0] = -1;
    a->pipe_entrada[1] = -1;

    parte = strtok_r(linea, ":", &resto);
    if (parte == NULL) return -1;
    while (*parte == ' ' || *parte == '\t') parte++;
    if (sscanf(parte, "%19s", a->id) != 1) return -1;

    parte = strtok_r(NULL, ":", &resto);
    if (parte == NULL) return -1;
    while (*parte == ' ' || *parte == '\t') parte++;
    limpiar_texto(parte);
    snprintf(a->nombre, sizeof(a->nombre), "%s", parte);

    parte = strtok_r(NULL, ":", &resto);
    if (parte == NULL) return -1;
    while (*parte == ' ' || *parte == '\t') parte++;
    limpiar_texto(parte);

    if (*parte == '\0') {
        a->tiempo = 100 + rand() % 4901;
    } else {
        char *fin;
        long valor = strtol(parte, &fin, 10);
        while (*fin == ' ' || *fin == '\t') fin++;
        if (*fin != '\0' || valor < 0 || valor > 2147483647L) return -1;
        a->tiempo = (int)valor;
    }

    parte = strtok_r(NULL, ":", &resto);
    if (parte != NULL) {
        char *token = strtok(parte, ",");
        while (token != NULL) {
            while (*token == ' ' || *token == '\t') token++;
            limpiar_texto(token);
            if (*token != '\0') {
                if (a->num_dependencias >= MAX_DEPENDENCIAS) return -1;
                snprintf(a->dependencias[a->num_dependencias], MAX_ID, "%s", token);
                a->num_dependencias++;
            }
            token = strtok(NULL, ",");
        }
    }

    return 1;
}

static int cargar_plan(const char *archivo) {
    FILE *f = fopen(archivo, "r");
    char linea[1024];
    int numero_linea = 0;

    if (f == NULL) {
        perror("Error al abrir el archivo");
        return -1;
    }

    while (fgets(linea, sizeof(linea), f) != NULL) {
        int resultado;
        numero_linea++;

        if (cantidad >= MAX_ACTIVIDADES) {
            fprintf(stderr, "Error: se superó el máximo de %d actividades.\n", MAX_ACTIVIDADES);
            fclose(f);
            return -1;
        }

        resultado = parsear_linea(linea, &actividades[cantidad]);
        if (resultado < 0) {
            fprintf(stderr, "Error de formato en la línea %d.\n", numero_linea);
            fclose(f);
            return -1;
        }
        if (resultado > 0) cantidad++;
    }

    fclose(f);
    return 0;
}

static int validar_dag(void) {
    int estado[MAX_ACTIVIDADES];
    int pila_nodos[MAX_ACTIVIDADES];
    int pila_dependencias[MAX_ACTIVIDADES];

    for (int i = 0; i < cantidad; i++) {
        estado[i] = 0;
        if (buscar_actividad(actividades[i].id) != i) {
            fprintf(stderr, "Error: ID duplicado: %s.\n", actividades[i].id);
            return -1;
        }
    }

    for (int i = 0; i < cantidad; i++) {
        for (int j = 0; j < actividades[i].num_dependencias; j++) {
            if (buscar_actividad(actividades[i].dependencias[j]) < 0) {
                fprintf(stderr, "Error: la actividad %s depende de %s, pero esa actividad no existe.\n",
                        actividades[i].id, actividades[i].dependencias[j]);
                return -1;
            }
        }
    }

    for (int inicio = 0; inicio < cantidad; inicio++) {
        if (estado[inicio] != 0) continue;

        int tope = 0;
        pila_nodos[tope] = inicio;
        pila_dependencias[tope] = 0;
        estado[inicio] = 1;

        while (tope >= 0) {
            int actual = pila_nodos[tope];
            int *pos = &pila_dependencias[tope];

            if (*pos >= actividades[actual].num_dependencias) {
                estado[actual] = 2;
                tope--;
                continue;
            }

            int dep = buscar_actividad(actividades[actual].dependencias[*pos]);
            (*pos)++;

            if (estado[dep] == 1) {
                fprintf(stderr, "Error: se detectó un ciclo que involucra la actividad %s.\n",
                        actividades[dep].id);
                return -1;
            }

            if (estado[dep] == 0) {
                if (tope + 1 >= MAX_ACTIVIDADES) {
                    fprintf(stderr, "Error: estructura de validación DAG agotada.\n");
                    return -1;
                }
                estado[dep] = 1;
                tope++;
                pila_nodos[tope] = dep;
                pila_dependencias[tope] = 0;
            }
        }
    }

    printf("Validación DAG: correcta.\n");
    return 0;
}

static int enviar_mensaje_fd(int fd, const char *mensaje) {
    size_t total = strlen(mensaje) + 1;
    size_t enviados = 0;

    while (enviados < total) {
        ssize_t n = write(fd, mensaje + enviados, total - enviados);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        enviados += (size_t)n;
    }
    return 0;
}

static int recibir_mensaje(int fd, char *buffer, size_t tam) {
    size_t recibidos = 0;

    while (recibidos + 1 < tam) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EINTR) {
                if (interrupcion) return -1;
                continue;
            }
            return -1;
        }
        if (c == '\0') {
            buffer[recibidos] = '\0';
            return 1;
        }
        buffer[recibidos++] = c;
    }

    buffer[recibidos] = '\0';
    return 1;
}

static int todas_dependencias_disponibles(int indice) {
    for (int i = 0; i < actividades[indice].num_dependencias; i++) {
        int dep = buscar_actividad(actividades[indice].dependencias[i]);
        if (dep < 0) return 0;
        if (actividades[dep].estado != TERMINADA && actividades[dep].estado != EJECUTANDO) {
            return 0;
        }
    }
    return 1;
}

static int alguna_dependencia_abortada(int indice) {
    for (int i = 0; i < actividades[indice].num_dependencias; i++) {
        int dep = buscar_actividad(actividades[indice].dependencias[i]);
        if (dep >= 0 && actividades[dep].estado == ABORTADA) {
            return 1;
        }
    }
    return 0;
}

static void abortar_rama(int indice) {
    if (indice < 0 || actividades[indice].estado == ABORTADA || actividades[indice].estado == TERMINADA) {
        return;
    }

    if (actividades[indice].estado == EJECUTANDO) {
        if (actividades[indice].pid > 0) {
            kill(actividades[indice].pid, SIGTERM);
        }
    } else {
        actividades[indice].estado = ABORTADA;
    }

    for (int i = 0; i < cantidad; i++) {
        for (int j = 0; j < actividades[i].num_dependencias; j++) {
            if (strcmp(actividades[i].dependencias[j], actividades[indice].id) == 0) {
                abortar_rama(i);
                break;
            }
        }
    }
}

static int crear_proceso(int indice) {
    Actividad *a = &actividades[indice];

    if (pipe(a->pipe_entrada) == -1) {
        perror("Error al crear pipe de entrada");
        return -1;
    }

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("Error al crear proceso");
        close(a->pipe_entrada[0]);
        close(a->pipe_entrada[1]);
        a->pipe_entrada[0] = -1;
        a->pipe_entrada[1] = -1;
        return -1;
    }

    if (pid == 0) {
        char mensaje[TAM_MENSAJE];
        int recibidas = 0;
        int fallo_forzado = 0;
        const char *fallar = getenv("PLANIFICADOR_FALLA");

        close(a->pipe_entrada[1]);
        close(pipe_completadas[0]);

        for (int i = 0; i < a->num_dependencias; i++) {
            int resultado = recibir_mensaje(a->pipe_entrada[0], mensaje, sizeof(mensaje));
            if (resultado <= 0 || interrupcion) {
                close(a->pipe_entrada[0]);
                _exit(2);
            }

            printf("[PID %d] %s recibió: %s\n", (int)getpid(), a->id, mensaje);
            fflush(stdout);

            if (strncmp(mensaje, "FALLIDA:", 8) == 0) {
                close(a->pipe_entrada[0]);
                snprintf(mensaje, sizeof(mensaje), "FALLIDA:%s", a->id);
                enviar_mensaje_fd(pipe_completadas[1], mensaje);
                _exit(2);
            }
            recibidas++;
        }

        close(a->pipe_entrada[0]);

        if (fallar != NULL && strcmp(fallar, a->id) == 0) {
            fallo_forzado = 1;
        }

        if (fallo_forzado) {
            printf("[PID %d] Falló %s\n", (int)getpid(), a->id);
            fflush(stdout);
            snprintf(mensaje, sizeof(mensaje), "FALLIDA:%s", a->id);
            enviar_mensaje_fd(pipe_completadas[1], mensaje);
            _exit(1);
        }

        printf("[PID %d] Ejecutando %s - %s (%d ms)\n",
               (int)getpid(), a->id, a->nombre, a->tiempo);
        fflush(stdout);

        int restante = a->tiempo;
        while (restante > 0 && !interrupcion) {
            int tramo = restante > 100 ? 100 : restante;
            struct timespec espera;
            espera.tv_sec = tramo / 1000;
            espera.tv_nsec = (long)(tramo % 1000) * 1000000L;
            nanosleep(&espera, NULL);
            restante -= tramo;
        }

        if (interrupcion) {
            snprintf(mensaje, sizeof(mensaje), "ABORTADA:%s", a->id);
            enviar_mensaje_fd(pipe_completadas[1], mensaje);
            _exit(2);
        }

        printf("[PID %d] Finalizó %s\n", (int)getpid(), a->id);
        fflush(stdout);

        snprintf(mensaje, sizeof(mensaje), "TERMINADA:%s", a->id);
        enviar_mensaje_fd(pipe_completadas[1], mensaje);
        _exit(0);
    }

    close(a->pipe_entrada[0]);
    a->pid = pid;
    a->estado = EJECUTANDO;

    printf("[PADRE] Creado PID %d para %s\n", (int)pid, a->id);
    fflush(stdout);
    return 0;
}

static int propagar_dependencia(int origen) {
    char mensaje[TAM_MENSAJE];
    snprintf(mensaje, sizeof(mensaje), "%s:%s",
             actividades[origen].estado == TERMINADA ? "TERMINADA" : "FALLIDA",
             actividades[origen].id);

    for (int i = 0; i < cantidad; i++) {
        for (int j = 0; j < actividades[i].num_dependencias; j++) {
            if (strcmp(actividades[i].dependencias[j], actividades[origen].id) == 0 &&
                actividades[i].estado == EJECUTANDO) {
                if (enviar_mensaje_fd(actividades[i].pipe_entrada[1], mensaje) == -1) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int contar_ejecutando(void) {
    int n = 0;
    for (int i = 0; i < cantidad; i++) {
        if (actividades[i].estado == EJECUTANDO) n++;
    }
    return n;
}

static int contar_finalizadas(void) {
    int n = 0;
    for (int i = 0; i < cantidad; i++) {
        if (actividades[i].estado == TERMINADA || actividades[i].estado == ABORTADA) n++;
    }
    return n;
}

static int procesar_completadas(int bloquear) {
    char mensaje[TAM_MENSAJE];

    if (!bloquear) {
        fd_set lectura;
        struct timeval tv;
        FD_ZERO(&lectura);
        FD_SET(pipe_completadas[0], &lectura);
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int listo = select(pipe_completadas[0] + 1, &lectura, NULL, NULL, &tv);
        if (listo <= 0 || !FD_ISSET(pipe_completadas[0], &lectura)) {
            return 0;
        }
    }

    int resultado = recibir_mensaje(pipe_completadas[0], mensaje, sizeof(mensaje));
    if (resultado <= 0) return resultado;

    char *dos_puntos = strchr(mensaje, ':');
    if (dos_puntos == NULL) return 1;
    *dos_puntos = '\0';

    int indice = buscar_actividad(dos_puntos + 1);
    if (indice >= 0 && actividades[indice].estado == EJECUTANDO) {
        if (strcmp(mensaje, "TERMINADA") == 0) {
            actividades[indice].estado = TERMINADA;
            printf("[PADRE] %s terminó correctamente.\n", actividades[indice].id);
        } else if (strcmp(mensaje, "FALLIDA") == 0 || strcmp(mensaje, "ABORTADA") == 0) {
            actividades[indice].estado = ABORTADA;
            printf("[PADRE] %s terminó con fallo. Se aborta su rama.\n", actividades[indice].id);
            abortar_rama(indice);
        }

        if (actividades[indice].pipe_entrada[1] != -1) {
            close(actividades[indice].pipe_entrada[1]);
            actividades[indice].pipe_entrada[1] = -1;
        }

        propagar_dependencia(indice);
    }

    return 1;
}

static void enviar_dependencias_ya_terminadas(int indice) {
    char mensaje[TAM_MENSAJE];
    Actividad *a = &actividades[indice];

    for (int i = 0; i < a->num_dependencias; i++) {
        int dep = buscar_actividad(a->dependencias[i]);
        if (dep >= 0 && (actividades[dep].estado == TERMINADA || actividades[dep].estado == ABORTADA)) {
            const char *tipo = actividades[dep].estado == TERMINADA ? "TERMINADA" : "FALLIDA";
            snprintf(mensaje, sizeof(mensaje), "%s:%s", tipo, actividades[dep].id);
            if (enviar_mensaje_fd(a->pipe_entrada[1], mensaje) == -1) {
                perror("Error al propagar mensaje por pipe");
            }
        }
    }
}

static void mostrar_estados(void) {
    printf("\n--- Estado final ---\n");
    for (int i = 0; i < cantidad; i++) {
        printf("%s : %s\n", actividades[i].id, nombre_estado(actividades[i].estado));
    }
}

int main(int argc, char *argv[]) {
    int k;
    int activos;

    /* 1. VERIFICAR ARGUMENTOS */
    if (argc != 3) {
        fprintf(stderr, "Uso: %s plan.txt K\n", argv[0]);
        return EXIT_FAILURE;
    }

    k = atoi(argv[2]);
    if (k <= 0) {
        fprintf(stderr, "Error: K debe ser mayor que 0.\n");
        return EXIT_FAILURE;
    }

    /* 2. CONFIGURAR SEÑAL */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejar_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    srand((unsigned int)(time(NULL) ^ getpid()));

    /* 3. LEER PLAN */
    if (cargar_plan(argv[1]) != 0) {
        return EXIT_FAILURE;
    }

    if (cantidad == 0) {
        fprintf(stderr, "Error: el archivo no contiene actividades.\n");
        return EXIT_FAILURE;
    }

    /* 4. VALIDAR DAG */
    if (validar_dag() != 0) {
        return EXIT_FAILURE;
    }

    printf("Archivo: %s\n", argv[1]);
    printf("Límite de concurrencia K: %d\n", k);
    printf("Actividades encontradas: %d\n", cantidad);

    for (int i = 0; i < cantidad; i++) {
        printf("Actividad %s: %s | Tiempo: %d ms | Dependencias: ",
               actividades[i].id, actividades[i].nombre, actividades[i].tiempo);
        if (actividades[i].num_dependencias == 0) {
            printf("ninguna");
        } else {
            for (int j = 0; j < actividades[i].num_dependencias; j++) {
                if (j > 0) printf(", ");
                printf("%s", actividades[i].dependencias[j]);
            }
        }
        printf("\n");
    }

    /* 5. CREAR PIPE CENTRAL */
    if (pipe(pipe_completadas) == -1) {
        perror("Error al crear pipe central");
        return EXIT_FAILURE;
    }

    /* 6. PLANIFICAR PROCESOS */
    while (contar_finalizadas() < cantidad) {
        if (interrupcion) {
            printf("\n[PADRE] SIGINT recibido. Abortando todas las actividades.\n");
            for (int i = 0; i < cantidad; i++) {
                if (actividades[i].estado == PENDIENTE) {
                    actividades[i].estado = ABORTADA;
                }
            }
            for (int i = 0; i < cantidad; i++) {
                if (actividades[i].estado == EJECUTANDO && actividades[i].pid > 0) {
                    kill(actividades[i].pid, SIGTERM);
                }
            }
            break;
        }

        int hubo_cambio = 0;
        activos = contar_ejecutando();

        /* 7. ABORTAR RAMAS INVALIDADAS */
        for (int i = 0; i < cantidad; i++) {
            if (actividades[i].estado == PENDIENTE && alguna_dependencia_abortada(i)) {
                abortar_rama(i);
                hubo_cambio = 1;
            }
        }

        /* 8. CREAR ACTIVIDADES DISPONIBLES */
        for (int i = 0; i < cantidad && activos < k; i++) {
            if (actividades[i].estado != PENDIENTE) continue;
            if (!todas_dependencias_disponibles(i)) continue;
            if (alguna_dependencia_abortada(i)) continue;

            if (crear_proceso(i) == 0) {
                enviar_dependencias_ya_terminadas(i);
                activos++;
                hubo_cambio = 1;
            } else {
                abortar_rama(i);
            }
        }

        /* 9. PROCESAR TERMINACIONES */
        int procesado = procesar_completadas(0);
        if (procesado > 0) {
            hubo_cambio = 1;
        }

        /* 10. RECOGER PROCESOS */
        for (int i = 0; i < cantidad; i++) {
            if (actividades[i].estado == TERMINADA || actividades[i].estado == ABORTADA) {
                if (actividades[i].pid > 0) {
                    waitpid(actividades[i].pid, NULL, WNOHANG);
                }
            }
        }

        if (!hubo_cambio && contar_ejecutando() > 0) {
            procesar_completadas(1);
        } else if (!hubo_cambio && contar_ejecutando() == 0) {
            int pendientes = 0;
            for (int i = 0; i < cantidad; i++) {
                if (actividades[i].estado == PENDIENTE) {
                    pendientes = 1;
                    break;
                }
            }
            if (pendientes) {
                fprintf(stderr, "Error interno: quedaron actividades pendientes sin poder ejecutarse.\n");
                for (int i = 0; i < cantidad; i++) {
                    if (actividades[i].estado == PENDIENTE) {
                        actividades[i].estado = ABORTADA;
                    }
                }
            }
        }
    }

    /* 11. FINALIZAR PROCESOS */
    if (interrupcion) {
        for (int i = 0; i < cantidad; i++) {
            if (actividades[i].estado == EJECUTANDO && actividades[i].pid > 0) {
                kill(actividades[i].pid, SIGTERM);
            }
        }
        for (int i = 0; i < cantidad; i++) {
            if (actividades[i].pid > 0) {
                waitpid(actividades[i].pid, NULL, 0);
            }
            if (actividades[i].estado == EJECUTANDO) {
                actividades[i].estado = ABORTADA;
            }
        }
    } else {
        for (int i = 0; i < cantidad; i++) {
            if (actividades[i].pid > 0) {
                waitpid(actividades[i].pid, NULL, 0);
            }
        }
    }

    /* 12. CERRAR PIPES */
    for (int i = 0; i < cantidad; i++) {
        if (actividades[i].pipe_entrada[0] != -1) close(actividades[i].pipe_entrada[0]);
        if (actividades[i].pipe_entrada[1] != -1) close(actividades[i].pipe_entrada[1]);
    }
    close(pipe_completadas[0]);
    close(pipe_completadas[1]);

    /* 13. MOSTRAR RESULTADO */
    mostrar_estados();
    return EXIT_SUCCESS;
}
