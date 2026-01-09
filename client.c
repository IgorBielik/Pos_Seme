#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>

#include "shared.h"

static int sock = -1;
static int playerId = -1;  // Unikátny ID hráča
static int gameId = -1;
static struct termios origTermios;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &origTermios);
    atexit(disable_raw_mode);
    
    struct termios raw = origTermios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Nastaví socket na non-blocking mode
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl");
    }
}

// Vykresľuje hru
static void render_game(const game_state_t *state) {
    system("clear");
    printf("=== HADÍK - Hra ID: %d ===\n", state->gameId);
    printf("Čas: %d s | Hráči: %d\n\n", state->elapsedTime, state->playerCount);
    
    // Vytvor mapu
    char map[WORLD_HEIGHT][WORLD_WIDTH];
    memset(map, ' ', sizeof(map));
    
    // Vlož ovocie
    for (int f = 0; f < state->foodCount; f++) {
        if (state->food[f].y >= 0 && state->food[f].y < WORLD_HEIGHT &&
            state->food[f].x >= 0 && state->food[f].x < WORLD_WIDTH) {
            map[state->food[f].y][state->food[f].x] = '*';
        }
    }
    
    // Vlož hadíkov
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].playerId == -1) continue;  // Voľný slot
        if (!state->snakes[i].alive) continue;
        char ch = '@' + i; // Rôzne znaky pre rôznych hráčov
        for (int j = 0; j < state->snakes[i].length; j++) {
            int x = state->snakes[i].body[j].x;
            int y = state->snakes[i].body[j].y;
            if (x >= 0 && x < WORLD_WIDTH && y >= 0 && y < WORLD_HEIGHT) {
                map[y][x] = (j == 0) ? ch : 'o'; // Hlava vs telo
            }
        }
    }
    
    // Vykresli mapu
    printf("┌");
    for (int x = 0; x < WORLD_WIDTH; x++) printf("─");
    printf("┐\n");
    
    for (int y = 0; y < WORLD_HEIGHT; y++) {
        printf("│");
        for (int x = 0; x < WORLD_WIDTH; x++) {
            printf("%c", map[y][x]);
        }
        printf("│\n");
    }
    
    printf("└");
    for (int x = 0; x < WORLD_WIDTH; x++) printf("─");
    printf("┘\n\n");
    
    // Vypíš skóre
    printf("SKÓRE:\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].playerId == -1) continue;  // Voľný slot
        char status[20] = "";
        if (!state->snakes[i].alive) {
            strcpy(status, "[MŔTVY]");
        } else if (state->snakes[i].paused) {
            strcpy(status, "[PAUZA]");
        }
        printf("  Hráč %d: %d bodov %s\n", i, state->snakes[i].score, status);
    }
    printf("\nPokyny: W/A/S/D - pohyb, P - menu, Q - odchod\n");
    if (!state->gameRunning) {
        printf("\n[HRA SKONČILA]\n");
    }
    
}

// Pošle vstup na server
static int send_input(action_t action, direction_t direction) {
    client_input_t input;
    input.playerId = playerId;
    input.action = action;
    input.direction = direction;
    input.gameId = gameId;
    
    ssize_t n = send(sock, &input, sizeof(input), 0);
    return n == sizeof(input) ? 0 : -1;
}

// Čaká na stav hry zo servera
static int recv_game_state(game_state_t *state) {
    ssize_t n = recv(sock, state, sizeof(game_state_t), 0);
    if (n == sizeof(game_state_t)) {
        gameId = state->gameId;
        return 0;
    }
    if (n == 0) {
        printf("Server zatvoril spojenie\n");
        return -1;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("recv");
        return -1;
    }
    return 1; // Žiadne dáta, retry
}
// Zobrazí menu a vráti voľbu (1-4 s aktívnou hrou, 1-3 bez nej)
static int show_menu(int has_active_game) {
    system("clear");
    printf("=== HADÍK - Menu ===\n");
    if (has_active_game) {
        printf("1. Pokračovať v hre\n");
        printf("2. Vytvoriť novú hru\n");
        printf("3. Pripojiť sa k inej hre\n");
        printf("4. Ukončiť program\n");
        printf("Zvoľ možnosť (1-4): ");
    } else {
        printf("1. Vytvoriť novú hru\n");
        printf("2. Pripojiť sa k hre\n");
        printf("3. Ukončiť program\n");
        printf("Zvoľ možnosť (1-3): ");
    }
    fflush(stdout);
    
    int choice = 0;
    if (scanf("%d", &choice) != 1) {
        return -1;
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    return choice;
}

// Spracuje menu a vráti: 0=pokračovanie, 1=nová hra, 2=join iná hra, -1=exit
// out_gid sa naplní ak ide o join
static int handle_menu(int *out_gid, int has_active_game, int active_game_id) {
    int choice = show_menu(has_active_game);
    
    if (choice < 0) {
        printf("Neplatný vstup\n");
        return -1;
    }
    

    if (choice == 1 && has_active_game) {
        // Pokračovať
        printf("Pokračujem v hre %d...\n", active_game_id);
        return 0;
    } else if ((choice == 1 && !has_active_game) || (choice == 2 && has_active_game)) {
        // Nová hra
        if (has_active_game) {
            send_input(ACTION_QUIT, DIR_NONE);
            usleep(100000);
        }
        printf("Vytváram novú hru...\n");
        return 1;
    } else if ((choice == 2 && !has_active_game) || (choice == 3 && has_active_game)) {
        // Join iná hra
        if (has_active_game) {
            send_input(ACTION_QUIT, DIR_NONE);
            usleep(100000);
        }
        
        printf("Zadaj ID hry (0-%d): ", MAX_PLAYERS - 1);
        fflush(stdout);
        if (scanf("%d", out_gid) != 1) {
            printf("Neplatný vstup\n");
            return -1;
        }
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        printf("Pripájam sa k hre %d...\n", *out_gid);
        return 2;
    } else {
        // Exit
        if (has_active_game) {
            send_input(ACTION_QUIT, DIR_NONE);
        }
        printf("Ukončujem...\n");
        return -1;
    }
}

// Spustí hru, vráti 1 ak sa hráč chce vrátiť do menu, 0 ak chce exit
static int play_game(int *out_old_game_id) {
    printf("Hra pripravená! Ovládanie: W/A/S/D, P - menu, Q - ukončiť\n");
    printf("Spúšťam za 2 sekundy...\n");
    sleep(2);
    
    // Raw mode
    enable_raw_mode();
    set_nonblocking(sock);
    
    direction_t currentDir = DIR_RIGHT;
    int running = 1;
    game_state_t state;
    memset(&state, 0, sizeof(state));
    
    // Načítaj aktuálny stav
    for (int i = 0; i < 20; i++) {
        if (recv_game_state(&state) == 0) break;
        usleep(50000);
    }
    render_game(&state);
    
    // Hlavný loop
    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sock, &rfds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        
        int ret = select(sock + 1, &rfds, NULL, NULL, &tv);
        
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                switch (ch) {
                    case 'w':
                    case 'W':
                        if (currentDir != DIR_DOWN) {
                            currentDir = DIR_UP;
                            send_input(ACTION_MOVE, currentDir);
                        }
                        break;
                    case 's':
                    case 'S':
                        if (currentDir != DIR_UP) {
                            currentDir = DIR_DOWN;
                            send_input(ACTION_MOVE, currentDir);
                        }
                        break;
                    case 'a':
                    case 'A':
                        if (currentDir != DIR_RIGHT) {
                            currentDir = DIR_LEFT;
                            send_input(ACTION_MOVE, currentDir);
                        }
                        break;
                    case 'd':
                    case 'D':
                        if (currentDir != DIR_LEFT) {
                            currentDir = DIR_RIGHT;
                            send_input(ACTION_MOVE, currentDir);
                        }
                        break;
                    case 'q':
                    case 'Q':
                    case 3:
                        send_input(ACTION_QUIT, DIR_NONE);
                        disable_raw_mode();
                        *out_old_game_id = -1;
                        return 0; // Exit program
                    case 'p':
                    case 'P':
                        // Pauza - vráť sa do menu
                        send_input(ACTION_PAUSE, DIR_NONE);
                        disable_raw_mode();
                        *out_old_game_id = gameId;
                        return 1; // Vráť sa do menu
                }
            }
        }
        
        if (ret > 0 && FD_ISSET(sock, &rfds)) {
            int recv_ret = recv_game_state(&state);
            if (recv_ret < 0) {
                running = 0;
            } else if (recv_ret == 0) {
                render_game(&state);
                if (!state.gameRunning) {
                    running = 0;
                }
            }
        }
    }
    
    disable_raw_mode();
    printf("\nHra skončila\n");
    *out_old_game_id = -1;
    return 1; // Vráť sa do menu
}

int main() {
    // 1. Vytvorenie socketu
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket failed");
        return 1;
    }
    
    // 2. Pripojenie na server
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        close(sock);
        return 1;
    }
    
    printf("Pripojený na server\n\n");
    
    // Vygeneruj unikátny ID hráča (podľa času + PID)
    playerId = (int)time(NULL) * 1000 + getpid();
    if (playerId < 0) playerId = -playerId;
    
    int oldGameId = -1;
    
    // Hlavná slučka - menu a hra
    while (1) {
        // Menu
        int has_active = (oldGameId >= 0);
        int join_game_id = -1;
        int menu_result = handle_menu(&join_game_id, has_active, oldGameId);
        
        if (menu_result == -1) {
            // Exit
            break;
        }
        
        if (menu_result == 0) {
            // Pokračovať - preskočí príjem nového stavu
            gameId = oldGameId;
        } else if (menu_result == 1) {
            // Nová hra
            gameId = -1;
            if (send_input(ACTION_CREATE_GAME, DIR_NONE) < 0) {
                perror("send failed");
                close(sock);
                return 1;
            }
            oldGameId = -1;
        } else if (menu_result == 2) {
            // Join iná hra
            gameId = join_game_id;
            if (send_input(ACTION_JOIN_GAME, DIR_NONE) < 0) {
                perror("send failed");
                close(sock);
                return 1;
            }
            oldGameId = -1;
        }
        
        // Čakaj na prvý stav (len ak to nie je pokračovanie)
        if (menu_result != 0) {
            printf("Čakám na server...\n");
            game_state_t state;
            memset(&state, 0, sizeof(state));
            
            int got_state = 0;
            for (int i = 0; i < 50 && !got_state; i++) {
                ssize_t n = recv(sock, &state, sizeof(game_state_t), 0);
                if (n == sizeof(game_state_t)) {
                    gameId = state.gameId;
                    oldGameId = gameId;
                    got_state = 1;
                    break;
                }
                usleep(100000);
            }
            
            if (!got_state) {
                printf("Nepodarilo sa získať stav hry\n");
                close(sock);
                return 1;
            }
        }
        
        // Spusti hru
        int play_result = play_game(&oldGameId);
        if (play_result == 0) {
            // Exit program
            break;
        }
        // play_result == 1 znamená vráť sa do menu, slučka pokračuje
    }
    
    printf("Odpájam sa...\n");
    close(sock);
    return 0;
}