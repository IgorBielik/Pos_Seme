#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "shared.h"
#include "game.h"

typedef struct {
    int fd;
    int player_idx; // index v state.snakes
    int active;
} client_slot_t;

static game_state_t state;
static client_slot_t clients[MAX_PLAYERS];
static int elapsed_ms = 0;

static void remove_client(int idx) {
    if (idx < 0 || idx >= state.player_count) return;
    if (clients[idx].active) close(clients[idx].fd);
    clients[idx].active = 0;
    game_remove_player(&state, idx);
}

static int add_client(int fd) {
    int idx = game_add_player(&state);
    if (idx < 0) return -1;
    clients[idx].fd = fd;
    clients[idx].player_idx = idx;
    clients[idx].active = 1;
    return idx;
}

static void process_input_wrapper(int idx, const client_input_t *input) {
    game_process_input(&state, idx, input);
}

static void broadcast_state(void) {
    for (int i = 0; i < state.player_count; i++) {
        if (!clients[i].active) continue;
        send(clients[i].fd, &state, sizeof(state), 0);
    }
}

int main(void) {
    srand((unsigned int)time(NULL));
    game_init(&state);
    for (int i = 0; i < FD_SETSIZE; i++) {
        clients[i].fd = -1;
        clients[i].player_idx = -1;
        clients[i].active = 0;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 8) < 0) {
        perror("listen failed");
        close(server_fd);
        return 1;
    }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        int maxfd = server_fd;

        for (int i = 0; i < state.player_count; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            }
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = GAME_LOOP_MS * 1000;

        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(server_fd, &rfds)) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                if (add_client(cfd) >= 0) {
                    printf("Client joined, players: %d\n", state.player_count);
                } else {
                    close(cfd); // full
                }
            }
        }

        for (int i = 0; i < state.player_count; i++) {
            if (!clients[i].active) continue;
            if (FD_ISSET(clients[i].fd, &rfds)) {
                client_input_t in;
                ssize_t n = recv(clients[i].fd, &in, sizeof(in), 0);
                if (n <= 0) {
                    remove_client(i);
                    printf("Client %d disconnected\n", i);
                } else if (n == sizeof(in)) {
                    process_input_wrapper(i, &in);
                }
            }
        }

        game_tick(&state);
        elapsed_ms += GAME_LOOP_MS;
        state.elapsed_time = elapsed_ms / 1000;
        broadcast_state();
    }

    for (int i = 0; i < state.player_count; i++) {
        if (clients[i].active) close(clients[i].fd);
    }
    close(server_fd);
    return 0;
}