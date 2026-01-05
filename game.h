#ifndef GAME_H
#define GAME_H

#include "shared.h"

// Inicializuje stav hry na prazdno
void game_init(game_state_t *state);

// Prida hraca, vrati index noveho hraca alebo -1 ak je plno
int game_add_player(game_state_t *state);

// Označí hráča ako neaktívneho (mŕtvy/odpojený)
void game_remove_player(game_state_t *state, int player_idx);

// Spracuje vstup klienta (smer/pauza/quit)
void game_process_input(game_state_t *state, int player_idx, const client_input_t *input);

// Jeden tick hernej logiky (pohyb, kolízie, ovocie, skóre)
void game_tick(game_state_t *state);

#endif // GAME_H
