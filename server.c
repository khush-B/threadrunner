#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd,
             (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port %d\n", port);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("Client connected\n");
unsigned char request[49];

size_t received = 0;

while (received < sizeof(request)) {

    ssize_t bytes = recv(
        client_fd,
        request + received,
        sizeof(request) - received,
        0
    );

    if (bytes < 0) {
        perror("recv");
        break;
    }

    if (bytes == 0) {
        printf("Client disconnected\n");
        break;
    }

    received += (size_t)bytes;
}

printf("Received %zu of %zu bytes\n",
       received,
       sizeof(request));

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
