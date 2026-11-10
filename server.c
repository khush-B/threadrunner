#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

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


/* Fixed workers and a bounded FIFO queue of accepted sockets. */
#define QUEUE_CAPACITY 1024
#define MAX_WORKERS 64

static struct {
    int sockets[QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} client_queue = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER
};

static void check_pthread(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static void queue_client(int fd)
{
    check_pthread(pthread_mutex_lock(&client_queue.mutex),
                  "pthread_mutex_lock");

    while (client_queue.count == QUEUE_CAPACITY) {
        check_pthread(
            pthread_cond_wait(&client_queue.not_full,
                              &client_queue.mutex),
            "pthread_cond_wait");
    }

    client_queue.sockets[client_queue.tail] = fd;
    client_queue.tail =
        (client_queue.tail + 1) % QUEUE_CAPACITY;
    client_queue.count++;

    check_pthread(pthread_cond_signal(&client_queue.not_empty),
                  "pthread_cond_signal");
    check_pthread(pthread_mutex_unlock(&client_queue.mutex),
                  "pthread_mutex_unlock");
}

static int next_client(void)
{
    check_pthread(pthread_mutex_lock(&client_queue.mutex),
                  "pthread_mutex_lock");

    while (client_queue.count == 0) {
        check_pthread(
            pthread_cond_wait(&client_queue.not_empty,
                              &client_queue.mutex),
            "pthread_cond_wait");
    }

    int fd = client_queue.sockets[client_queue.head];
    client_queue.head =
        (client_queue.head + 1) % QUEUE_CAPACITY;
    client_queue.count--;

    check_pthread(pthread_cond_signal(&client_queue.not_full),
                  "pthread_cond_signal");
    check_pthread(pthread_mutex_unlock(&client_queue.mutex),
                  "pthread_mutex_unlock");

    return fd;
}

static int worker_count(size_t *count)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    *count = cpus > 0 ? (size_t)cpus : 1;

    if (*count > MAX_WORKERS) {
        *count = MAX_WORKERS;
    }

    const char *value = getenv("THREADRUNNER_WORKERS");
    if (value != NULL) {
        char *end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(value, &end, 10);

        if (errno != 0 || value[0] == '\0' || *end != '\0'
            || parsed < 1 || parsed > MAX_WORKERS) {
            fprintf(stderr,
                    "THREADRUNNER_WORKERS must be 1 to %d\n",
                    MAX_WORKERS);
            return -1;
        }

        *count = (size_t)parsed;
    }

    return 0;
}

static void handle_client(int client_fd)
{
    unsigned char request[PACKET_REQUEST_SIZE];
    if (receive_request(client_fd, request) != 1) {
        close(client_fd);
        return;
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


static void *worker_main(void *unused)
{
    (void)unused;

    while (1) {
        int client_fd = next_client();
        handle_client(client_fd);
    }

    return NULL;
}

static int start_worker_pool(void)
{
    size_t count;
    if (worker_count(&count) != 0) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        pthread_t thread;
        int error = pthread_create(
            &thread, NULL, worker_main, NULL);

        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n",
                    strerror(error));
            return -1;
        }

        check_pthread(pthread_detach(thread), "pthread_detach");
    }

    printf("Worker pool: %zu workers, queue capacity %d\n",
           count, QUEUE_CAPACITY);
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
    if (start_worker_pool() != 0) {
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

        queue_client(client_fd);
    }
}
