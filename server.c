#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <inttypes.h>
#include "messages.h"
#include <errno.h>

#define LONESHA256_IMPLEMENTATION
#include "lonesha256.h"

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
unsigned char request[PACKET_REQUEST_SIZE];

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



    if (received != sizeof(request)) {
    close(client_fd);
    continue;
}

unsigned char hash[SHA256_DIGEST_LENGTH];
uint64_t start_network;
uint64_t end_network;

memcpy(hash,
       request + PACKET_REQUEST_HASH_OFFSET,
       sizeof(hash));

memcpy(&start_network,
       request + PACKET_REQUEST_START_OFFSET,
       sizeof(start_network));

memcpy(&end_network,
       request + PACKET_REQUEST_END_OFFSET,
       sizeof(end_network));

uint64_t start = be64toh(start_network);
uint64_t end = be64toh(end_network);
uint8_t priority = request[PACKET_REQUEST_PRIO_OFFSET];

printf("Hash: ");
for (size_t i = 0; i < sizeof(hash); i++) {
    printf("%02x", (unsigned int)hash[i]);
}
printf("\n");

printf("Start: %" PRIu64 "\n", start);
printf("End: %" PRIu64 "\n", end);
printf("Priority: %u\n", (unsigned int)priority);

/* Search [start, end): start is included, end is excluded. */
uint64_t answer = 0;
int found = 0;

for (uint64_t candidate = start; candidate < end; candidate++) {
    uint64_t candidate_le = htole64(candidate);
    unsigned char candidate_hash[SHA256_DIGEST_LENGTH];

    if (lonesha256(candidate_hash,
                  (const unsigned char *)&candidate_le,
                  sizeof(candidate_le)) != 0) {
        fprintf(stderr, "SHA256 calculation failed\n");
        break;
    }

    if (memcmp(candidate_hash, hash, sizeof(hash)) == 0) {
        answer = candidate;
        found = 1;
        break;
    }

}
if (!found) {
    fprintf(stderr, "No matching number found in the range\n");
    close(client_fd);
    continue;
}

printf("Answer: %" PRIu64 "\n", answer);

/* Encode the answer in network byte order. */
uint64_t answer_network = htobe64(answer);
unsigned char response[PACKET_RESPONSE_SIZE];

memcpy(response + PACKET_RESPONSE_ANSWER_OFFSET,
       &answer_network,
       sizeof(answer_network));

/* TCP may send fewer bytes than requested. */
size_t sent = 0;

while (sent < sizeof(response)) {
    ssize_t bytes = send(client_fd,
                         response + sent,
                         sizeof(response) - sent,
                         MSG_NOSIGNAL);

    if (bytes < 0) {
        if (errno == EINTR) {
            continue;
        }
        perror("send");
        break;
    }

    if (bytes == 0) {
        fprintf(stderr, "send made no progress\n");
        break;
    }

    sent += (size_t)bytes;
}

printf("Sent %zu of %zu bytes\n", sent, sizeof(response));
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
