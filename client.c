#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define PORT 12345

int main() {
    // 1. vytvorenie socketu
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    // 2. adresa servera
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // 3. pripojenie na server
    connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    // 4. odoslanie správy
    char msg[] = "Hello server!";
    write(sock, msg, strlen(msg)+1);

    close(sock);
}