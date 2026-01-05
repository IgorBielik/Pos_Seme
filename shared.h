#ifndef SHARED_H
#define SHARED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 12345
#define MAX_PLAYERS 10
#define MAX_SNAKE_LENGTH 500
#define WORLD_WIDTH 40
#define WORLD_HEIGHT 20
#define GAME_LOOP_MS 500

// Smer pohybu
typedef enum {
    DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT
} direction_t;

// Akcia od klienta
typedef enum {
    ACTION_MOVE,      // Zmena smeru
    ACTION_PAUSE,     // Pauza
    ACTION_QUIT       // Ukončenie
} action_t;

// Pozícia
typedef struct {
    int x;
    int y;
} position_t;

// Hadík
typedef struct {
    int player_id;
    position_t body[MAX_SNAKE_LENGTH];
    int length;
    direction_t direction;
    int score;
    int alive;
} snake_t;

// Stav hry (Server → Client)
typedef struct {
    int elapsed_time;           // Čas od začiatku v sekundách
    snake_t snakes[MAX_PLAYERS];
    int player_count;
    
    // Ovocie
    position_t food[MAX_PLAYERS];
    int food_count;
    
    int game_running;
} game_state_t;

// Vstup od klienta (Client → Server)
typedef struct {
    action_t action;
    direction_t direction;  // Pre ACTION_MOVE
} client_input_t;

#endif