#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        return -1;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    printf("Connected to server. Sending request in chunks...\n");

    const char *chunk1 = "GET / HTTP/1.1\r\n";
    const char *chunk2 = "Host: localhost:8080\r\n";
    const char *chunk3 = "Connection:keep-alive\r\nContent-Length:10\r\n\r\n123";
    const char *chunk4 = "45";
    const char *chunk5 = "67890GET / HTTP/1.1\r\nHost: localhost:8080\r\nConnection:keep-alive\r\nContent-Length:5\r\n\r\n12345";
    const char *chunk6 = "GET";
    const char *chunk7 = " / HTTP/1.1\r\n";
    const char *chunk8 = "Connection:close\r\nContent-Length:5\r\nHost:localhost:8080\r\n\r\n12345";

    printf("Sending chunk 1...\n");
    send(sock, chunk1, strlen(chunk1), 0);
    sleep(1);

    printf("Sending chunk 2...\n");
    send(sock, chunk2, strlen(chunk2), 0);
    sleep(1);

    printf("Sending chunk 3...\n");
    send(sock, chunk3, strlen(chunk3), 0);
    sleep(1);

    printf("Sending chunk 4...\n");
    send(sock, chunk4, strlen(chunk4), 0);
    sleep(1);

    printf("Sending chunk 5...\n");
    send(sock, chunk5, strlen(chunk5), 0);

    printf("Sending chunk 6...\n");
    send(sock, chunk6, strlen(chunk6), 0);

    printf("Sending chunk 7...\n");
    send(sock, chunk7, strlen(chunk7), 0);

    printf("Sending chunk 8...\n");
    send(sock, chunk8, strlen(chunk8), 0);

    printf("Full request sent.\n");
    printf("Response from the server:\n\n");
    char buffer[1024] = {0};
    ssize_t bytes_read = 0;
    while ((bytes_read = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }

    close(sock);
    return 0;
}
