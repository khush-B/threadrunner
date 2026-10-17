#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "messages.h"
#define LONESHA256_IMPLEMENTATION
#include "lonesha256.h"

/* Return 1 for a complete request, 0 for EOF, and -1 for an I/O error. */
static int receive_request(int fd, unsigned char request[PACKET_REQUEST_SIZE])
{
    size_t received = 0;

    while (received < PACKET_REQUEST_SIZE) {
        ssize_t bytes = recv(fd, request + received,
                             PACKET_REQUEST_SIZE - received, 0);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            return -1;
        }
        if (bytes == 0) {
            fprintf(stderr, "Incomplete request: %zu of %d bytes\n",
                    received, PACKET_REQUEST_SIZE);
            return 0;
        }
        received += (size_t)bytes;
    }
    return 1;
}

/* A separate found flag allows zero to be a valid answer. */
static int reverse_hash(const unsigned char hash[SHA256_DIGEST_LENGTH],
                        uint64_t start, uint64_t end, uint64_t *answer)
{
    for (uint64_t candidate = start; candidate < end; candidate++) {
        uint64_t candidate_le = htole64(candidate);
        unsigned char candidate_hash[SHA256_DIGEST_LENGTH];

        if (lonesha256(candidate_hash,
                      (const unsigned char *)&candidate_le,
                      sizeof(candidate_le)) != 0) {
            fprintf(stderr, "SHA256 calculation failed\n");
            return -1;
        }
        if (memcmp(candidate_hash, hash, sizeof(candidate_hash)) == 0) {
            *answer = candidate;
            return 1;
        }
    }
    return 0;
}

static int send_answer(int fd, uint64_t answer)
{
    uint64_t answer_network = htobe64(answer);
    unsigned char response[PACKET_RESPONSE_SIZE];
    memcpy(response + PACKET_RESPONSE_ANSWER_OFFSET,
           &answer_network, sizeof(answer_network));

    size_t sent = 0;
    while (sent < sizeof(response)) {
        ssize_t bytes = send(fd, response + sent, sizeof(response) - sent,
                             MSG_NOSIGNAL);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send");
            return -1;
        }
        if (bytes == 0) {
            fprintf(stderr, "send made no progress\n");
            return -1;
        }
        sent += (size_t)bytes;
    }
    printf("Sent %zu of %zu bytes\n", sent, sizeof(response));
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    errno = 0;
    char *port_end = NULL;
    unsigned long port = strtoul(argv[1], &port_end, 10);
    if (errno != 0 || argv[1][0] == '\0' || *port_end != '\0'
        || port == 0 || port > UINT16_MAX) {
        fprintf(stderr, "Port must be an integer from 1 to 65535\n");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    printf("Server listening on port %lu\n", port);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        unsigned char request[PACKET_REQUEST_SIZE];
        if (receive_request(client_fd, request) != 1) {
            close(client_fd);
            continue;
        }

        unsigned char hash[SHA256_DIGEST_LENGTH];
        uint64_t start_network;
        uint64_t end_network;
        memcpy(hash, request + PACKET_REQUEST_HASH_OFFSET, sizeof(hash));
        memcpy(&start_network, request + PACKET_REQUEST_START_OFFSET,
               sizeof(start_network));
        memcpy(&end_network, request + PACKET_REQUEST_END_OFFSET,
               sizeof(end_network));

        uint64_t start = be64toh(start_network);
        uint64_t end = be64toh(end_network);
        uint8_t priority = request[PACKET_REQUEST_PRIO_OFFSET];
        printf("Received %d of %d bytes\n",
               PACKET_REQUEST_SIZE, PACKET_REQUEST_SIZE);
        printf("Start: %" PRIu64 "\nEnd: %" PRIu64 "\nPriority: %u\n",
               start, end, (unsigned int)priority);

        uint64_t answer = 0;
        int found = reverse_hash(hash, start, end, &answer);
        if (found == 1) {
            printf("Answer: %" PRIu64 "\n", answer);
            (void)send_answer(client_fd, answer);
        } else if (found == 0) {
            fprintf(stderr, "No matching number found in the range\n");
        }
        close(client_fd);
    }
}
