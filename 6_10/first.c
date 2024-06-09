#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

// Функция для создания клиентского сокета и подключения к серверу
int initializeClientSocket(char *ipAddress, int port) {
    int socketDescriptor;

    // Создание сокета
    if ((socketDescriptor = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("Creation os socket failed");
        exit(EXIT_FAILURE);
    }

    // Настройка адреса сервера
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(ipAddress);
    serverAddr.sin_port = htons(port);

    // Подключение к серверу
    if (connect(socketDescriptor, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection to server went wrong");
        exit(EXIT_FAILURE);
    }

    return socketDescriptor;
}

// Функция для отправки задачи на сервер и обработки ответа
void sendTaskAndAwaitResponse(int clientSocket, struct Task task) {
    int serverResponse;
    int bytesReceived;

    // Отправка задачи на сервер
    if (send(clientSocket, &task, sizeof(task), 0) != sizeof(task)) {
        perror("Error sending task");
        exit(EXIT_FAILURE);
    }

    // Ожидание ответа от сервера
    if ((bytesReceived = recv(clientSocket, &serverResponse, sizeof(int), 0)) != sizeof(int)) {
        perror("Error receiving response");
        exit(EXIT_FAILURE);
    }

    // Повторная отправка задачи до получения подтверждения
    while (serverResponse != 1) {
        if (send(clientSocket, &task, sizeof(task), 0) != sizeof(task)) {
            perror("Error resending task");
            exit(EXIT_FAILURE);
        }

        if ((bytesReceived = recv(clientSocket, &serverResponse, sizeof(int), 0)) != sizeof(int)) {
            perror("Error receiving response");
            exit(EXIT_FAILURE);
        }
    }

    // Вывод информации о выполненной задаче
    if (task.status != 1) {
        printf("Gardener %d at row: %d, col: %d\n", task.worker_id, task.row, task.col);
    }
}

// Функция, которая выполняет задачи на поле
void processField(int clientSocket, int duration, struct FieldDimensions field) {
    struct Task task;
    task.worker_id = 1;  // Идентификатор садовника
    task.duration = duration;
    int totalRows = field.numRows;
    int totalCols = field.numCols;

    int i = 0, j = 0;
    task.status = 0;

    // Проход по полю змейкой
    while (i < totalRows) {
        while (j < totalCols) {
            task.row = i;
            task.col = j;
            sendTaskAndAwaitResponse(clientSocket, task);
            ++j;
        }

        ++i;
        --j;

        while (j >= 0) {
            task.row = i;
            task.col = j;
            sendTaskAndAwaitResponse(clientSocket, task);
            --j;
        }

        ++i;
        ++j;
    }

    // Завершение работы
    task.status = 1;
    sendTaskAndAwaitResponse(clientSocket, task);
}

int main(int argc, char *argv[]) {
    int clientSocket;
    unsigned short serverPort;
    int duration;
    char *serverIp;
    char tempBuffer[256];
    int receivedBytes, totalReceivedBytes;

    // Проверка количества аргументов командной строки
    if (argc != 4) {
        fprintf(stderr, "Arguments: %s <server IP> <server port> <work time>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    serverIp = argv[1];
    serverPort = atoi(argv[2]);
    duration = atoi(argv[3]);

    // Инициализация клиентского сокета
    clientSocket = initializeClientSocket(serverIp, serverPort);

    struct FieldDimensions fieldSize;

    // Получение размеров поля от сервера
    if ((receivedBytes = recv(clientSocket, &fieldSize, sizeof(fieldSize), 0)) != sizeof(fieldSize)) {
        perror("Error receiving field size");
        exit(EXIT_FAILURE);
    }

    // Выполнение работы на поле
    processField(clientSocket, duration, fieldSize);

    printf("Work is done (1sr gardener)\n");
    close(clientSocket);
    return 0;
}

