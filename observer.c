#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

// Структура, описывающая задачу
struct Task {
    int row;          // Номер строки
    int col;          // Номер столбца
    int worker_id;    // Идентификатор садовника
    int duration;     // Время выполнения
    int status;       // Статус задачи
};

// Структура, описывающая размер поля
struct FieldDimensions {
    int numRows;      // Количество строк
    int numCols;      // Количество столбцов
};

// Перечисление типов событий
enum EventType { MAP_EVENT, ACTION_EVENT, META_EVENT };

// Структура, описывающая событие
struct Event {
    char timestamp[26];   // Временная метка
    char data[1024];      // Буфер данных
    enum EventType type;  // Тип события
};

// Структура для описания наблюдателя
struct Observer {
    int socket;
    int is_new;
    int is_active;
};

// Создание клиентского сокета и подключение к серверу
int establishConnection(const char *server_ip, int server_port) {
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Error: Unable to create socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(server_ip);
    server_address.sin_port = htons(server_port);

    if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Error: Unable to connect to server");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

// Обработка задачи и отправка на сервер
void sendTask(int sock, struct Task task) {
    int response;
    while (1) {
        if (send(sock, &task, sizeof(task), MSG_NOSIGNAL) != sizeof(task)) {
            fprintf(stderr, "Server connection lost...\n");
            close(sock);
            exit(EXIT_FAILURE);
        }

        if (recv(sock, &response, sizeof(int), MSG_NOSIGNAL) != sizeof(int)) {
            fprintf(stderr, "Server connection lost...\n");
            close(sock);
            exit(EXIT_FAILURE);
        }

        if (response == 1) break;
    }

    if (task.status != 1) {
        printf("Gardener %d at row: %d, col: %d\n", task.worker_id, task.row, task.col);
    }
}

// Глобальный клиентский сокет
int client_socket;

// Обработчик сигнала прерывания (Ctrl+C)
void signalHandler(int sig) {
    printf("Observer stopped\n");
    close(client_socket);
    exit(EXIT_SUCCESS);
}

// Главная функция
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Arguments: %s <server IP> <observer port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    client_socket = establishConnection(server_ip, server_port);

    signal(SIGINT, signalHandler);

    while (1) {
        char buffer[1024];
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received < 0) {
            perror("Receiving failed");
            close(client_socket);
            exit(EXIT_FAILURE);
        }

        if (bytes_received == 0) {
            printf("Server connection closed...\n");
            close(client_socket);
            exit(EXIT_SUCCESS);
        }

        buffer[bytes_received] = '\0';
        printf("%s", buffer);
    }

    close(client_socket);
    return 0;
}

