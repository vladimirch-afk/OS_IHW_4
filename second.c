#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Описание задачи
struct Task {
    int plot_i;
    int plot_j;
    int gardener_id;
    int working_time;
    int status;
};

// Описание размера поля
struct FieldSize {
    int rows;
    int columns;
};

// Типы событий
enum event_type { MAP, ACTION, META_INFO };

// Описание события
struct Event {
    char timestamp[26];
    char buffer[1024];
    enum event_type type;
};

// Создание клиентского сокета и подключение к серверу
int initializeSocket(const char *ip, int port) {
    int sockfd;
    
    // Создание сокета
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ip);
    serverAddr.sin_port = htons(port);
    
    // Подключение к серверу
    if (connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    
    return sockfd;
}

// Отправка задачи и получение ответа
void processTask(int sockfd, struct Task *task) {
    int response;
    
    do {
        if (send(sockfd, task, sizeof(*task), 0) != sizeof(*task)) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
        
        if (recv(sockfd, &response, sizeof(response), 0) != sizeof(response)) {
            perror("Receive failed");
            exit(EXIT_FAILURE);
        }
    } while (response != 1);
    
    if (task->status != 1) {
        printf("Gardener %d at row: %d, col: %d\n", task->gardener_id, task->plot_i, task->plot_j);
    }
}

// Выполнение задач на поле
void performWork(int sockfd, int duration, struct FieldSize size) {
    struct Task task = { .gardener_id = 2, .working_time = duration, .status = 0 };
    int i = size.rows - 1, j = size.columns - 1;

    // Обход поля змейкой снизу вверх
    while (j >= 0) {
        while (i >= 0) {
            task.plot_i = i;
            task.plot_j = j;
            processTask(sockfd, &task);
            --i;
        }

        --j;
        ++i;

        while (i < size.rows) {
            task.plot_i = i;
            task.plot_j = j;
            processTask(sockfd, &task);
            ++i;
        }

        --i;
        --j;
    }

    // Завершение работы
    task.status = 1;
    processTask(sockfd, &task);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Arguments: %s <server IP> <server port> <work time>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    int work_time = atoi(argv[3]);

    // Инициализация клиентского сокета
    int sockfd = initializeSocket(server_ip, server_port);

    struct FieldSize fieldSize;
    if (recv(sockfd, &fieldSize, sizeof(fieldSize), 0) != sizeof(fieldSize)) {
        perror("Failed to receive field size");
        exit(EXIT_FAILURE);
    }

    // Выполнение работы
    performWork(sockfd, work_time, fieldSize);

    printf("Work is done (2nd gardener)\n");
    close(sockfd);
    return 0;
}

