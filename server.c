#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "shared.h"
#include "game.h"

typedef struct {
    int fd;
    int player_idx; // index v games[game_id].snakes
    int game_id;    // ID hry, ktorej patrí klient
    int active;
} client_slot_t;

static game_state_t games[MAX_PLAYERS];
static client_slot_t clients[MAX_PLAYERS];
static int elapsed_ms[MAX_PLAYERS] = {0};
static pthread_t game_threads[MAX_PLAYERS];
static pthread_mutex_t games_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static int find_free_game_slot(void) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!games[i].game_running && games[i].player_count == 0) {
            return i;
        }
    }
    return -1;
}

static void broadcast_to_game(int game_id) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].active && clients[i].game_id == game_id) {
            send(clients[i].fd, &games[game_id], sizeof(game_state_t), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void* game_thread(void* arg) {
    int gid = *(int*)arg;
    free(arg);
    
    printf("Game thread %d started\n", gid);
    
    while (1) {
        pthread_mutex_lock(&games_mutex);
        
        if (!games[gid].game_running && games[gid].player_count == 0) {
            pthread_mutex_unlock(&games_mutex);
            break;
        }
        
        game_tick(&games[gid]);
        elapsed_ms[gid] += GAME_LOOP_MS;
        games[gid].elapsed_time = elapsed_ms[gid] / 1000;
        
        pthread_mutex_unlock(&games_mutex);
        
        broadcast_to_game(gid);
        usleep(GAME_LOOP_MS * 1000);
    }
    
    printf("Game thread %d ended\n", gid);
    return NULL;
}

static int create_new_game(void) {
    int gid = find_free_game_slot();
    if (gid < 0) return -1;
    
    pthread_mutex_lock(&games_mutex);
    game_init(&games[gid]);
    games[gid].game_id = gid;
    pthread_mutex_unlock(&games_mutex);
    
    // Spusti vlákno pre túto hru
    int *arg = malloc(sizeof(int));
    *arg = gid;
    if (pthread_create(&game_threads[gid], NULL, game_thread, arg) != 0) {
        perror("pthread_create failed");
        free(arg);
        return -1;
    }
    pthread_detach(game_threads[gid]);
    
    return gid;
}

static void remove_client(int client_idx) {
    if (client_idx < 0 || client_idx >= MAX_PLAYERS) return;
    
    pthread_mutex_lock(&clients_mutex);
    if (!clients[client_idx].active) {
        pthread_mutex_unlock(&clients_mutex);
        return;
    }
    
    int gid = clients[client_idx].game_id;
    int pidx = clients[client_idx].player_idx;
    
    close(clients[client_idx].fd);
    clients[client_idx].active = 0;
    pthread_mutex_unlock(&clients_mutex);
    
    if (gid >= 0) {
        pthread_mutex_lock(&games_mutex);
        game_remove_player(&games[gid], pidx);
        pthread_mutex_unlock(&games_mutex);
    }
}

static void process_input_wrapper(int client_idx, const client_input_t *input) {
    pthread_mutex_lock(&clients_mutex);
    if (client_idx < 0 || !clients[client_idx].active) {
        pthread_mutex_unlock(&clients_mutex);
        return;
    }
    int gid = clients[client_idx].game_id;
    int pidx = clients[client_idx].player_idx;
    pthread_mutex_unlock(&clients_mutex);
    
    pthread_mutex_lock(&games_mutex);
    game_process_input(&games[gid], pidx, input);
    pthread_mutex_unlock(&games_mutex);
}

int main(void) {
    srand((unsigned int)time(NULL));
    
    // Inicializuj prázdne štruktúry (hry sa vytvoria na požiadanie)
    memset(games, 0, sizeof(games));
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        clients[i].fd = -1;
        clients[i].player_idx = -1;
        clients[i].game_id = -1;
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
    printf("Press 'q' and Enter to shutdown the server...\n");
    
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server_fd, &rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = server_fd;
        
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (clients[i].active) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            }
        }
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            perror("select");
            break;
        }
        
        // Check if user wants to quit
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0 && (ch == 'q' || ch == 'Q')) {
                printf("\nShutting down server...\n");
                break;
            }
        }
        
        if (FD_ISSET(server_fd, &rfds)) {
            int cfd = accept(server_fd, NULL, NULL);
            if (cfd >= 0) {
                pthread_mutex_lock(&clients_mutex);
                int slot = -1;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (!clients[i].active) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= 0) {
                    clients[slot].fd = cfd;
                    clients[slot].game_id = -1;
                    clients[slot].player_idx = -1;
                    clients[slot].active = 1;
                    printf("Client %d connected, waiting for action\n", slot);
                } else {
                    close(cfd);
                    printf("Rejected connection, server full\n");
                }
                pthread_mutex_unlock(&clients_mutex);
            }
        }
        
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!clients[i].active) continue;
            if (FD_ISSET(clients[i].fd, &rfds)) {
                client_input_t in;
                ssize_t n = recv(clients[i].fd, &in, sizeof(in), 0);
                if (n <= 0) {
                    remove_client(i);
                    printf("Client %d disconnected\n", i);
                } else if (n == sizeof(in)) {
                    pthread_mutex_lock(&clients_mutex);
                    int has_game = clients[i].game_id >= 0;
                    pthread_mutex_unlock(&clients_mutex);
                    
                    // Ak klient ešte nemá hru, musí vytvoriť alebo pripojiť
                    if (!has_game) {
                        if (in.action == ACTION_CREATE_GAME) {
                            int gid = create_new_game();
                            if (gid >= 0) {
                                pthread_mutex_lock(&games_mutex);
                                int pidx = game_add_player(&games[gid]);
                                pthread_mutex_unlock(&games_mutex);
                                
                                pthread_mutex_lock(&clients_mutex);
                                clients[i].game_id = gid;
                                clients[i].player_idx = pidx;
                                pthread_mutex_unlock(&clients_mutex);
                                
                                printf("Client %d created game %d\n", i, gid);
                                broadcast_to_game(gid);
                            }
                        } else if (in.action == ACTION_JOIN_GAME) {
                            int gid = in.game_id;
                            pthread_mutex_lock(&games_mutex);
                            int can_join = (gid >= 0 && gid < MAX_PLAYERS && games[gid].game_running);
                            int pidx = -1;
                            if (can_join) {
                                pidx = game_add_player(&games[gid]);
                            }
                            pthread_mutex_unlock(&games_mutex);
                            
                            if (pidx >= 0) {
                                pthread_mutex_lock(&clients_mutex);
                                clients[i].game_id = gid;
                                clients[i].player_idx = pidx;
                                pthread_mutex_unlock(&clients_mutex);
                                printf("Client %d joined game %d\n", i, gid);
                                broadcast_to_game(gid);
                            }
                        }
                    } else {
                        
                        process_input_wrapper(i, &in);
                    }
                }
            }
        }
    }

    // Cleanup: zatvori všetky klientske sockety
    printf("Shutting down...\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].active) {
            close(clients[i].fd);
            clients[i].active = 0;
        }
    }
    
    // Počkaj na všetky vlákna (optional, pretože sme ich detachli)
    sleep(1);
    
    // Zničit mutexy
    pthread_mutex_destroy(&games_mutex);
    pthread_mutex_destroy(&clients_mutex);
    
    close(server_fd);
    printf("Server shutdown complete\n");
    return 0;
}