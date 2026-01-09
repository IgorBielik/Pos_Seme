#include "game.h"
#include <stdlib.h>
#include <string.h>

static int rand_between(int min, int max) {
    return min + rand() % (max - min + 1);
}

static int positions_equal(position_t a, position_t b) {
    return a.x == b.x && a.y == b.y;
}

static int active_players(const game_state_t *state) {
    int alive = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].alive) alive++;
    }
    return alive;
}

static int cell_occupied(const game_state_t *state, position_t p) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!state->snakes[i].alive) continue;
        for (int j = 0; j < state->snakes[i].length; j++) {
            if (positions_equal(state->snakes[i].body[j], p)) return 1;
        }
    }
    for (int f = 0; f < state->food_count; f++) {
        if (positions_equal(state->food[f], p)) return 1;
    }
    return 0;
}

static position_t random_free_position(const game_state_t *state) {
    position_t p;
    do {
        p.x = rand_between(0, WORLD_WIDTH - 1);
        p.y = rand_between(0, WORLD_HEIGHT - 1);
    } while (cell_occupied(state, p));
    return p;
}

static int opposite(direction_t a, direction_t b) {
    return (a == DIR_UP && b == DIR_DOWN) || (a == DIR_DOWN && b == DIR_UP) ||
           (a == DIR_LEFT && b == DIR_RIGHT) || (a == DIR_RIGHT && b == DIR_LEFT);
}

static void spawn_food_if_needed(game_state_t *state) {
    int target = active_players(state);
    if (target < 1) target = 1; // aspoň jedno ovocie, ak hra beží
    while (state->food_count < target && state->food_count < MAX_PLAYERS * 2) {
        state->food[state->food_count++] = random_free_position(state);
    }
}

static void move_snake(game_state_t *state, snake_t *s) {
    if (!s->alive || s->paused) return;

    position_t head = s->body[0];
    switch (s->direction) {
        case DIR_UP:    head.y = (head.y - 1 + WORLD_HEIGHT) % WORLD_HEIGHT; break;
        case DIR_DOWN:  head.y = (head.y + 1) % WORLD_HEIGHT; break;
        case DIR_LEFT:  head.x = (head.x - 1 + WORLD_WIDTH) % WORLD_WIDTH; break;
        case DIR_RIGHT: head.x = (head.x + 1) % WORLD_WIDTH; break;
        default: break;
    }

    // kolízia s telom alebo inými hadmi
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!state->snakes[i].alive) continue;
        int len = state->snakes[i].length;
        for (int j = 0; j < len; j++) {
            if (positions_equal(head, state->snakes[i].body[j])) {
                s->alive = 0;
                return;
            }
        }
    }

    int ate = 0;
    for (int f = 0; f < state->food_count; f++) {
        if (positions_equal(head, state->food[f])) {
            ate = 1;
            s->score += 10;
            state->food[f] = state->food[state->food_count - 1];
            state->food_count--;
            break;
        }
    }

    // posun tela
    for (int i = s->length; i > 0; i--) {
        s->body[i] = s->body[i - 1];
    }
    s->body[0] = head;
    if (ate && s->length < MAX_SNAKE_LENGTH) {
        s->length++;
    }
}

void game_init(game_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->game_running = 0;
    // Inicializuj player_id na -1 (označuje voľné miesto)
    for (int i = 0; i < MAX_PLAYERS; i++) {
        state->snakes[i].player_id = -1;
    }
}

void game_reset(game_state_t *state) {
    int old_game_id = state->game_id;  // Ulož game_id pred resetom
    state->game_running = 0;
    state->player_count = 0;
    state->food_count = 0;
    state->elapsed_time = 0;
    memset(state->snakes, 0, sizeof(state->snakes));
    memset(state->food, 0, sizeof(state->food));
    // Resetuj player_id na -1
    for (int i = 0; i < MAX_PLAYERS; i++) {
        state->snakes[i].player_id = -1;
    }
    state->game_id = old_game_id;  // Obnov game_id
}

int game_add_player(game_state_t *state, int player_id) {
    int idx = -1;
    // Skontroluj, či hráč už v hre existuje
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].player_id == player_id && state->snakes[i].alive) {
            return i; // Hráč už existuje a žije
        }
    }
    
    // Skontroluj, či hráč zomrel v tejto hre - ak áno, vráť -2
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].player_id == player_id && !state->snakes[i].alive) {
            return -2; // Hráč je mŕtvy - nedovoľ reconnect
        }
    }
    
    // Hľadaj voľné miesto
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].player_id == -1) {  // Voľný slot
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        return -1; // Plná hra
    }

    snake_t *s = &state->snakes[idx];
    memset(s, 0, sizeof(*s));

    s->player_id = player_id;  // Použij podaný player_id
    s->length = 3;
    s->direction = DIR_RIGHT;
    s->score = 0;
    s->alive = 1;
    s->paused = 0;

    position_t head = random_free_position(state);
    s->body[0] = head;
    s->body[1] = (position_t){(head.x - 1 + WORLD_WIDTH) % WORLD_WIDTH, head.y};
    s->body[2] = (position_t){(head.x - 2 + WORLD_WIDTH) % WORLD_WIDTH, head.y};

    state->player_count++;  // Zvýš počet hráčov
    state->game_running = 1;
    spawn_food_if_needed(state);
    return idx;
}

void game_remove_player(game_state_t *state, int player_idx, int permanent) {
    if (player_idx < 0 || player_idx >= MAX_PLAYERS) return;
    if (state->snakes[player_idx].player_id == -1) return;  // Už je voľný slot
    
    printf("Removing player %d from game %d (permanent=%d)\n", player_idx, state->game_id, permanent);
    
    // Zníž player_count iba ak bol hráč živý
    if (state->snakes[player_idx].alive && state->player_count > 0) {
        state->player_count--;
    }
    
    if (permanent) {
        // Úplné resetovanie - oslobodi slot pre ďalšieho hráča
        memset(&state->snakes[player_idx], 0, sizeof(snake_t));
        state->snakes[player_idx].player_id = -1;  // Označí slot ako voľný
    } else {
        // Mrtvy hráč - označí ako mŕtveho, ale nechá player_id
        state->snakes[player_idx].alive = 0;
        state->snakes[player_idx].paused = 0;
    }
}

void game_process_input(game_state_t *state, int player_id, const client_input_t *input) {
    if (!input) return;
    
    // Nájdi hadíka s daným player_id
    int player_idx = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->snakes[i].player_id == player_id) {
            player_idx = i;
            break;
        }
    }
    
    if (player_idx < 0) return;  // Hráč neexistuje
    
    snake_t *s = &state->snakes[player_idx];
    if (!s->alive) return;

    switch (input->action) {
        case ACTION_MOVE:
            if (!opposite(s->direction, input->direction)) {
                s->direction = input->direction;
            }
            s->paused = 0; // Resume on move
            break;
        case ACTION_QUIT:
            s->alive = 0;
            break;
        case ACTION_PAUSE:
            s->paused = !s->paused; // Toggle pause
            break;
        default:
            break;
    }
}

void game_tick(game_state_t *state) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        move_snake(state, &state->snakes[i]);
    }
    spawn_food_if_needed(state);
    int alive = active_players(state);
    state->player_count = alive; 
    state->game_running = alive > 0;
}