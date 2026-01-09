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
#include <errno.h>

#include "shared.h"
#include "game.h"

typedef struct ClientSlot {
    int fd;
    int playerId;  // Unikátny ID hráča
    int playerIdx; // index v games[gameId].snakes
    int gameId;    // ID hry, ktorej patrí klient
    int active;
} client_slot_t;

static game_state_t games[MAX_PLAYERS];
static client_slot_t clients[MAX_PLAYERS];
static int elapsedMs[MAX_PLAYERS] = {0};
static pthread_t gameThreads[MAX_PLAYERS];
static pthread_mutex_t gamesMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;



static int find_free_game_slot(void) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!games[i].gameRunning && games[i].playerCount == 0) {
            return i;
        }
    }
    return -1;
}

static void broadcast_to_game(int gameId) {
    pthread_mutex_lock(&clientsMutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clients[i].active && clients[i].gameId == gameId) {
            // Pošli stav všetkým hráčom
            send(clients[i].fd, &games[gameId], sizeof(game_state_t), 0);
            
            // Ak je hra skončená, odpoji klienta z tejto hry
            if (!games[gameId].gameRunning) {
                clients[i].gameId = -1;
                clients[i].playerIdx = -1;
                printf("Client %d released from finished game %d\n", i, gameId);
            }
        }
    }
    pthread_mutex_unlock(&clientsMutex);
}

void* game_thread(void* arg) {
    int gid = *(int*)arg;
    free(arg);
    
    printf("Game thread %d started\n", gid);
    
    while (1) {
        pthread_mutex_lock(&gamesMutex);
        
        if (!games[gid].gameRunning) {
            printf("Game %d has no players, terminating thread\n", gid);
            game_reset(&games[gid]);
            elapsedMs[gid] = 0;
            pthread_mutex_unlock(&gamesMutex);
            break;
        }
        
        game_tick(&games[gid]);
        elapsedMs[gid] += GAME_LOOP_MS;
        games[gid].elapsedTime = elapsedMs[gid] / 1000;
        
        pthread_mutex_unlock(&gamesMutex);
        
        broadcast_to_game(gid);
        
        usleep(GAME_LOOP_MS * 1000);
    }
    
    printf("Game thread %d ended\n", gid);
    return NULL;
}

static int create_new_game(void) {
    int gid = find_free_game_slot();
    if (gid < 0) return -1;
    
    pthread_mutex_lock(&gamesMutex);
    game_init(&games[gid]);
    games[gid].gameId = gid;
    pthread_mutex_unlock(&gamesMutex);
    
    // Spusti vlákno pre túto hru
    int *arg = malloc(sizeof(int));
    *arg = gid;
    if (pthread_create(&gameThreads[gid], NULL, game_thread, arg) != 0) {
        perror("pthread_create failed");
        free(arg);
        return -1;
    }
    pthread_detach(gameThreads[gid]);
    
    return gid;
}

static void remove_client(int client_idx) {
    if (client_idx < 0 || client_idx >= MAX_PLAYERS) return;
    
    pthread_mutex_lock(&clientsMutex);
    if (!clients[client_idx].active) {
        pthread_mutex_unlock(&clientsMutex);
        return;
    }
    
    int gid = clients[client_idx].gameId;
    int pidx = clients[client_idx].playerIdx;
    
    close(clients[client_idx].fd);
    clients[client_idx].active = 0;
    pthread_mutex_unlock(&clientsMutex);
    
    if (gid >= 0) {
        pthread_mutex_lock(&gamesMutex);
        game_remove_player(&games[gid], pidx, 0);  // 0 = hráč sa môže vrátiť
        pthread_mutex_unlock(&gamesMutex);
    }
}

static void process_input_wrapper(int client_idx, const client_input_t *input) {
    pthread_mutex_lock(&clientsMutex);
    if (client_idx < 0 || !clients[client_idx].active) {
        pthread_mutex_unlock(&clientsMutex);
        return;
    }
    int gid = clients[client_idx].gameId;
    int pidx = clients[client_idx].playerIdx;
    int playerId = clients[client_idx].playerId;
    pthread_mutex_unlock(&clientsMutex);
    
    pthread_mutex_lock(&gamesMutex);
    
    // Keď mŕtvy hráč AKÁKOĽVEK AKCIU vykoná, oslobodíme ho z hry
    if (pidx >= 0 && pidx < MAX_PLAYERS && !games[gid].snakes[pidx].alive) {
        game_remove_player(&games[gid], pidx, 1);  // 1 = permanent
        pthread_mutex_unlock(&gamesMutex);
        
        pthread_mutex_lock(&clientsMutex);
        clients[client_idx].gameId = -1;
        clients[client_idx].playerIdx = -1;
        pthread_mutex_unlock(&clientsMutex);
        return;
    }
    
    game_process_input(&games[gid], playerId, input);
    pthread_mutex_unlock(&gamesMutex);
}

int main(void) {
    srand((unsigned int)time(NULL));
    
    // Inicializuj prázdne štruktúry (hry sa vytvoria na požiadanie)
    memset(games, 0, sizeof(games));
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        clients[i].fd = -1;
        clients[i].playerId = -1;
        clients[i].playerIdx = -1;
        clients[i].gameId = -1;
        clients[i].active = 0;
    }

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(serverFd);
        return 1;
    }
    
    if (listen(serverFd, 8) < 0) {
        perror("listen failed");
        close(serverFd);
        return 1;
    }

    printf("Server listening on port %d\n", PORT);
    printf("Press 'q' and Enter to shutdown the server...\n");
    
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(serverFd, &rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = serverFd;
        
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
        
        if (FD_ISSET(serverFd, &rfds)) {
            int cfd = accept(serverFd, NULL, NULL);
            if (cfd >= 0) {
                pthread_mutex_lock(&clientsMutex);
                int slot = -1;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (!clients[i].active) {
                        slot = i;
                        break;
                    }
                }
                if (slot >= 0) {
                    clients[slot].fd = cfd;
                    clients[slot].gameId = -1;
                    clients[slot].playerIdx = -1;
                    clients[slot].active = 1;
                    printf("Client %d connected, waiting for action\n", slot);
                } else {
                    close(cfd);
                    printf("Rejected connection, server full\n");
                }
                pthread_mutex_unlock(&clientsMutex);
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
                } else {
                    // Ulož player_id z vstupu
                    pthread_mutex_lock(&clientsMutex);
                    clients[i].playerId = in.playerId;
                    int has_game = clients[i].gameId >= 0;
                    int oldGameId = clients[i].gameId;
                    int oldPlayerIdx = clients[i].playerIdx;
                    pthread_mutex_unlock(&clientsMutex);
                    
                    // Ak klient chce odísť zo svojej hry
                    if (has_game && in.action == ACTION_QUIT) {
                        pthread_mutex_lock(&gamesMutex);
                        game_remove_player(&games[oldGameId], oldPlayerIdx, 1);  // 1 = úplné oslobodenie
                        pthread_mutex_unlock(&gamesMutex);
                        
                        pthread_mutex_lock(&clientsMutex);
                        clients[i].gameId = -1;
                        clients[i].playerIdx = -1;
                        pthread_mutex_unlock(&clientsMutex);
                        
                        printf("Client %d quit game %d\n", i, oldGameId);
                    }
                    // Vytvor novú hru (quit volaný pred týmto)
                    else if (!has_game && in.action == ACTION_CREATE_GAME) {
                        int gid = create_new_game();
                        if (gid >= 0) {
                            pthread_mutex_lock(&gamesMutex);
                            int pidx = game_add_player(&games[gid], in.playerId);
                            pthread_mutex_unlock(&gamesMutex);
                            
                            if (pidx >= 0) {
                                pthread_mutex_lock(&clientsMutex);
                                clients[i].playerId = in.playerId;
                                clients[i].gameId = gid;
                                clients[i].playerIdx = pidx;
                                pthread_mutex_unlock(&clientsMutex);
                                
                                printf("Client %d created game %d\n", i, gid);
                                usleep(500000);
                                broadcast_to_game(gid);
                            } else {
                                printf("Player %d cannot create game (dead/full)\n", in.playerId);
                                send(clients[i].fd, &games[gid], sizeof(game_state_t), 0);
                            }
                        }
                    }
                    // Pripoj sa k existujúcej hre (klient už poslal QUIT pred týmto)
                    else if (!has_game && in.action == ACTION_JOIN_GAME) {
                        int gid = in.gameId;
                        pthread_mutex_lock(&gamesMutex);
                        int can_join = (gid >= 0 && gid < MAX_PLAYERS && games[gid].gameRunning);
                        int pidx = -1;
                        if (can_join) {
                            pidx = game_add_player(&games[gid], in.playerId);
                        }
                        pthread_mutex_unlock(&gamesMutex);
                        
                        if (pidx >= 0) {
                            pthread_mutex_lock(&clientsMutex);
                            clients[i].playerId = in.playerId;
                            clients[i].gameId = gid;
                            clients[i].playerIdx = pidx;
                            pthread_mutex_unlock(&clientsMutex);
                            printf("Client %d joined game %d\n", i, gid);
                            usleep(500000);
                            broadcast_to_game(gid);
                        } else {
                            printf("Client %d cannot join game %d (result=%d)\n", i, gid, pidx);
                            // Pošli stav hry aby vedel, že sa nepridá
                            if (gid >= 0 && gid < MAX_PLAYERS) {
                                send(clients[i].fd, &games[gid], sizeof(game_state_t), 0);
                            }
                        }
                    }
                    // Iné akcie (MOVE, PAUSE) spracuj v aktívnej hre
                    else if (has_game) {
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
    pthread_mutex_destroy(&gamesMutex);
    pthread_mutex_destroy(&clientsMutex);
    
    close(serverFd);
    printf("Server shutdown complete\n");
    return 0;
}