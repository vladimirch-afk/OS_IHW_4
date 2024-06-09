#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <time.h>

const char *shared_object = "/posix-shared-object";
const char *sem_shared_object = "/posix-sem-shared-object";
int pipe_fd[2];

struct Task {
    int plot_i;
    int plot_j;
    int gardener_id;
    int working_time;
    int status;
};

struct FieldSize {
    int rows;
    int columns;
};

enum event_type { MAP, ACTION, META_INFO };

struct Event {
    char timestamp[26];
    char buffer[1024];
    enum event_type type;
};

#define PLOTS 2

int createClientSocket(char *server_ip, int server_port) {
    int client_socket;

    if ((client_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Not able to create socket");
        exit(-1);
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(server_ip);
    server_address.sin_port = htons(server_port);

    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Not able to connect to server");
        exit(-1);
    }

    return client_socket;
}

void sendHandleRequest(int client_socket, struct Task task) {
    int status;
    int received;
    do {
        if (send(client_socket, &task, sizeof(task), 0) != sizeof(task)) {
            perror("Send error");
            exit(-1);
        }

        if ((received = recv(client_socket, &status, sizeof(int), 0)) != sizeof(int)) {
            perror("Receive error");
            exit(-1);
        }
    } while (status != 1);

    if (task.status != 1) {
        printf("Gardener %d on row: %d, col: %d\n", task.gardener_id, task.plot_i, task.plot_j);
    }
}

int createServerSocket(in_addr_t sin_addr, int port) {
    int server_socket;
    struct sockaddr_in server_address;

    if ((server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Unable to create server socket");
        exit(-1);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Unable to bind address");
        exit(-1);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Unable to listen server socket");
        exit(-1);
    }

    return server_socket;
}

int acceptClientConnection(int server_socket) {
    int client_socket;
    struct sockaddr_in client_address;
    unsigned int address_length;

    address_length = sizeof(client_address);

    if ((client_socket =
             accept(server_socket, (struct sockaddr *)&client_address, &address_length)) < 0) {
        perror("Unable to accept client connection");
        exit(-1);
    }

    printf("Client %s:%d\n", inet_ntoa(client_address.sin_addr), client_address.sin_port);

    return client_socket;
}

void printField(int *field, int columns, int rows) {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < columns; ++j) {
            if (field[i * columns + j] < 0) {
                printf("X ");
            } else {
                printf("%d ", field[i * columns + j]);
            }
        }
        printf("\n");
    }

    fflush(stdout);
}

void sprintField(char *buffer, int *field, int columns, int rows) {
    int offset = 0;
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < columns; ++j) {
            if (field[i * columns + j] < 0) {
                offset += sprintf(buffer + offset, "X ");
            } else {
                offset += sprintf(buffer + offset, "%d ", field[i * columns + j]);
            }
        }
        offset += sprintf(buffer + offset, "\n");
    }
}

void setEventWithCurrentTime(struct Event *event) {
    time_t timer;
    struct tm *tm_info;
    timer = time(NULL);
    tm_info = localtime(&timer);
    strftime(event->timestamp, sizeof(event->timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
}

void writeEventToPipe(struct Event *event) {
    if (write(pipe_fd[1], event, sizeof(*event)) < 0) {
        perror("Can't write to pipe");
        exit(-1);
    }
}

void handleGardenPlot(sem_t *semaphores, int *field, int columns, struct Task task) {
    sem_wait(semaphores + (task.plot_i / 2 * (columns / 2) + task.plot_j / 2));

    struct Event gardener_event;
    setEventWithCurrentTime(&gardener_event);
    gardener_event.type = ACTION;
    sprintf(gardener_event.buffer, "Gardener %d takes (row: %d, col: %d) plot\n", task.gardener_id,
            task.plot_i, task.plot_j);
    writeEventToPipe(&gardener_event);

    if (field[task.plot_i * columns + task.plot_j] == 0) {
        field[task.plot_i * columns + task.plot_j] = task.gardener_id;
        usleep(task.working_time * 1000);
    } else {
        usleep(task.working_time / PLOTS * 1000);
    }

    struct Event event;
    setEventWithCurrentTime(&event);
    sprintf(event.buffer, "\n");
    sprintField(event.buffer + 1, field, columns, columns);
    event.type = MAP;
    writeEventToPipe(&event);

    sem_post(semaphores + (task.plot_i / 2 * (columns / 2) + task.plot_j / 2));
}

void handle(int client_socket, sem_t *semaphores, int *field, struct FieldSize field_size) {
    char buffer[256];
    int bytes_received;

    if (send(client_socket, (char *)(&field_size), sizeof(field_size), 0) != sizeof(field_size)) {
        perror("Sending error");
        exit(-1);
    }

    struct Task task;
    const int plot_handle_status = 1;

    if ((bytes_received = recv(client_socket, buffer, sizeof(struct Task), 0)) < 0) {
        perror("Receive error");
        exit(-1);
    }
    task = *((struct Task *)buffer);

    while (task.status != 1) {
        handleGardenPlot(semaphores, field, field_size.columns, task);

        if (send(client_socket, &plot_handle_status, sizeof(int), 0) != sizeof(int)) {
            perror("Sending error");
            exit(-1);
        }

        if ((bytes_received = recv(client_socket, buffer, sizeof(struct Task), 0)) < 0) {
            perror("Receiving bad");
            exit(-1);
        }
        task = *((struct Task *)buffer);
    }

    if (send(client_socket, &plot_handle_status, sizeof(int), 0) != sizeof(int)) {
        perror("send() bad");
        exit(-1);
    }

    close(client_socket);
}

int *getField(int field_size) {
    int *field;
    int shmid;

    if ((shmid = shm_open(shared_object, O_CREAT | O_RDWR, 0666)) < 0) {
        perror("Cannot connect to shared memory");
        exit(-1);
    } else {
        if (ftruncate(shmid, field_size * sizeof(int)) < 0) {
            perror("Cannot resize shared memory");
            exit(-1);
        }
        if ((field = mmap(0, field_size * sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED, shmid,
                          0)) < 0) {
            printf("Cannot connect to shared memory\n");
            exit(-1);
        };
    }

    return field;
}

void initializeField(int *field, int rows, int columns) {
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < columns; ++j) {
            field[i * columns + j] = 0;
        }
    }

    int percentage = 10 + random() % 20;
    int count_of_bad_plots = columns * rows * percentage / 100;
    for (int i = 0; i < count_of_bad_plots; ++i) {
        int row_index;
        int column_index;
        do {
            row_index = random() % rows;
            column_index = random() % columns;
        } while (field[row_index * columns + column_index] == -1);

        field[row_index * columns + column_index] = -1;
    }
}

void createSemaphores(sem_t *semaphores, int count) {
    for (int k = 0; k < count; ++k) {
        if (sem_init(semaphores + k, 1, 1) < 0) {
            perror("sem_init: can not create semaphore");
            exit(-1);
        };

        int val;
        sem_getvalue(semaphores + k, &val);
        if (val != 1) {
            printf(
                "Semaphore can't set initial value to 1. Restart\n");
            shm_unlink(shared_object);
            exit(-1);
        }
    }
}

sem_t *createSemaphoresSharedMemory(int sem_count) {
    int sem_main_shmid;
    sem_t *semaphores;

    if ((sem_main_shmid = shm_open(sem_shared_object, O_CREAT | O_RDWR, 0666)) < 0) {
        perror("Can't connect to shared memory");
        exit(-1);
    } else {
        if (ftruncate(sem_main_shmid, sem_count * sizeof(sem_t)) < 0) {
            perror("Can't rezie shm");
            exit(-1);
        }
        if ((semaphores = mmap(0, sem_count * sizeof(sem_t), PROT_WRITE | PROT_READ, MAP_SHARED,
                               sem_main_shmid, 0)) < 0) {
            printf("Can\'t connect to shared memory for semaphores\n");
            exit(-1);
        };
    }

    return semaphores;
}

void writeInfoToConsole() {
    while (1) {
        struct Event event;
        if (read(pipe_fd[0], &event, sizeof(event)) < 0) {
            perror("Can't read from pipe");
            exit(-1);
        }
        if (event.type == MAP) {
            printf("%s\n", event.buffer);
        }
    }
}

pid_t runWriter() {
    pid_t child_id;
    if ((child_id = fork()) < 0) {
        perror("Unable to create child for handling write to log");
        exit(-1);
    } else if (child_id == 0) {
        writeInfoToConsole();
        exit(0);
    }

    return child_id;
}

int server_socket;
int children_counter = 0;

void waitChildProcessess() {
    while (children_counter > 0) {
        int child_id = waitpid((pid_t)-1, NULL, 0);
        if (child_id < 0) {
            perror("Unable to wait child proccess");
            exit(-1);
        } else {
            children_counter--;
        }
    }
}

void sigint_handler(int signum) {
    printf("Server stopped\n");
    waitChildProcessess();
    shm_unlink(shared_object);
    shm_unlink(sem_shared_object);
    close(server_socket);
    exit(0);
}

int main(int argc, char *argv[]) {

    if (argc != 4) {
        fprintf(stderr, "Arguments:  %s <server IP> <server port> <grid size>\n", argv[0]);
        exit(1);
    }

    in_addr_t server_address;
    if ((server_address = inet_addr(argv[1])) < 0) {
        perror("Invalid server address");
        exit(-1);
    }

    int server_port = atoi(argv[2]);
    if (server_port < 0) {
        perror("Invalid server port");
        exit(-1);
    }

    if (pipe(pipe_fd) < 0) {
        perror("Can't open pipe");
        exit(-1);
    }

    runWriter();

    signal(SIGINT, sigint_handler);

    int square_side_size = atoi(argv[3]);
    if (square_side_size > 10 || square_side_size < 2) {
        perror("Square side size should be in range [2, 10]");
        exit(-1);
    }

    int rows = 2 * square_side_size;
    int columns = 2 * square_side_size;
    int sem_count = rows * columns / 4;

    int *field = getField(rows * columns);
    initializeField(field, rows, columns);

    sem_t *semaphores = createSemaphoresSharedMemory(sem_count);
    createSemaphores(semaphores, sem_count);

    server_socket = createServerSocket(server_address, server_port);
    printField(field, columns, rows);
    while (1) {
        int client_socket = acceptClientConnection(server_socket);

        pid_t child_id;
        if ((child_id = fork()) < 0) {
            perror("Unable to create child for handling new connection");
            exit(-1);
        } else if (child_id == 0) {
            struct FieldSize field_size;
            field_size.columns = columns;
            field_size.rows = rows;

            signal(SIGINT, SIG_DFL);
            close(server_socket);
            handle(client_socket, semaphores, field, field_size);
            exit(0);
        }

        printf("child process: %d\n", (int)child_id);
        close(client_socket);
        children_counter++;
    }

    return 0;
}
