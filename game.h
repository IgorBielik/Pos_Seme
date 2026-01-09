#ifndef GAME_H
#define GAME_H

#include "shared.h"

// Inicializuje stav hry na prazdno
void game_init(game_state_t *state);

// Resetuje hru po skončení (vyčisti player_count a resources)
void game_reset(game_state_t *state);

// Prida hraca, vrati index noveho hraca alebo -1 ak je plno
int game_add_player(game_state_t *state, int playerId);

// Označí hráča ako neaktívneho (mŕtvy/odpojený)
// permanent=1: resetuje slot úplne (aby sa mohol pripojiť ďalší)
// permanent=0: iba označí ako mŕtveho (hráč sa môže vrátiť)
void game_remove_player(game_state_t *state, int playerIdx, int permanent);

// Spracuje vstup klienta (smer/pauza/quit) podľa player_id
void game_process_input(game_state_t *state, int playerId, const client_input_t *input);

// Jeden tick hernej logiky (pohyb, kolízie, ovocie, skóre)
void game_tick(game_state_t *state);

#endif // GAME_H
