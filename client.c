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

#include "shared.h"

static int sock = -1;
static int game_id = -1;
static struct termios orig_termios;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
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
    printf("=== HADÍK - Hra ID: %d ===\n", state->game_id);
    printf("Čas: %d s | Hráči: %d\n\n", state->elapsed_time, state->player_count);
    
    // Vytvor mapu
    char map[WORLD_HEIGHT][WORLD_WIDTH];
    memset(map, ' ', sizeof(map));
    
    // Vlož ovocie
    for (int f = 0; f < state->food_count; f++) {
        if (state->food[f].y >= 0 && state->food[f].y < WORLD_HEIGHT &&
            state->food[f].x >= 0 && state->food[f].x < WORLD_WIDTH) {
            map[state->food[f].y][state->food[f].x] = '*';
        }
    }
    
    // Vlož hadíkov
    for (int i = 0; i < state->player_count; i++) {
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
    for (int i = 0; i < state->player_count; i++) {
        if (state->snakes[i].alive) {
            printf("  Hráč %d: %d bodov\n", i, state->snakes[i].score);
        }
    }
    printf("\nPokyny: W/A/S/D - pohyb, P - pauza, Q - odchod\n");
    if (!state->game_running) {
        printf("\n[HRA SKONČILA]\n");
    }
    
}

// Pošle vstup na server
static int send_input(action_t action, direction_t direction) {
    client_input_t input;
    input.action = action;
    input.direction = direction;
    input.game_id = game_id;
    
    ssize_t n = send(sock, &input, sizeof(input), 0);
    return n == sizeof(input) ? 0 : -1;
}

// Čaká na stav hry zo servera
static int recv_game_state(game_state_t *state) {
    ssize_t n = recv(sock, state, sizeof(game_state_t), 0);
    if (n == sizeof(game_state_t)) {
        game_id = state->game_id;
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
    
    // 3. Menu
    printf("=== HADÍK - Menu ===\n");
    printf("1. Vytvoriť novú hru\n");
    printf("2. Pripojiť sa k hre\n");
    printf("Zvoľ možnosť (1/2): ");
    fflush(stdout);
    
    int choice = 0;
    if (scanf("%d", &choice) != 1) {
        printf("Neplatný vstup\n");
        close(sock);
        return 1;
    }
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    // 4. Odošli akciu
    if (choice == 1) {
        printf("Vytváram novú hru...\n");
        if (send_input(ACTION_CREATE_GAME, DIR_RIGHT) < 0) {
            perror("send failed");
            close(sock);
            return 1;
        }
    } else if (choice == 2) {
        printf("Zadaj ID hry (0-%d): ", MAX_PLAYERS - 1);
        fflush(stdout);
        int gid = 0;
        if (scanf("%d", &gid) != 1) {
            printf("Neplatný vstup\n");
            close(sock);
            return 1;
        }
        while ((c = getchar()) != '\n' && c != EOF);
        
        game_id = gid;
        printf("Pripájam sa k hre %d...\n", gid);
        if (send_input(ACTION_JOIN_GAME, DIR_RIGHT) < 0) {
            perror("send failed");
            close(sock);
            return 1;
        }
    } else {
        printf("Neplatná voľba\n");
        close(sock);
        return 1;
    }
    
    // 5. Čakaj na prvý stav
    printf("Čakám na server...\n");
    game_state_t state;
    memset(&state, 0, sizeof(state));
    
    int got_state = 0;
    for (int i = 0; i < 50 && !got_state; i++) {
        ssize_t n = recv(sock, &state, sizeof(game_state_t), 0);
        if (n == sizeof(game_state_t)) {
            game_id = state.game_id;
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
    
    printf("Hra pripravená! Ovládanie: W/A/S/D, Q - ukončiť\n");
    printf("Spúšťam za 2 sekundy...\n");
    sleep(2);
    
    // 6. Raw mode
    enable_raw_mode();
    set_nonblocking(sock);
    
    direction_t current_dir = DIR_RIGHT;
    int running = 1;
    render_game(&state);
    
    // 7. Hlavný loop
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
                        if (current_dir != DIR_DOWN) {
                            current_dir = DIR_UP;
                            send_input(ACTION_MOVE, current_dir);
                        }
                        break;
                    case 's':
                    case 'S':
                        if (current_dir != DIR_UP) {
                            current_dir = DIR_DOWN;
                            send_input(ACTION_MOVE, current_dir);
                        }
                        break;
                    case 'a':
                    case 'A':
                        if (current_dir != DIR_RIGHT) {
                            current_dir = DIR_LEFT;
                            send_input(ACTION_MOVE, current_dir);
                        }
                        break;
                    case 'd':
                    case 'D':
                        if (current_dir != DIR_LEFT) {
                            current_dir = DIR_RIGHT;
                            send_input(ACTION_MOVE, current_dir);
                        }
                        break;
                    case 'q':
                    case 'Q':
                    case 3:
                        send_input(ACTION_QUIT, DIR_NONE);
                        running = 0;
                        break;
                }
            }
        }
        
        if (ret > 0 && FD_ISSET(sock, &rfds)) {
            int recv_ret = recv_game_state(&state);
            if (recv_ret < 0) {
                running = 0;
            } else if (recv_ret == 0) {
                render_game(&state);
                if (!state.game_running) {
                    running = 0;
                }
            }
        }
    }
    
    disable_raw_mode();
    printf("\nOdpájam sa...\n");
    close(sock);
    return 0;
}