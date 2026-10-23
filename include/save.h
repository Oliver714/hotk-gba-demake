#ifndef SANGO_SAVE_H
#define SANGO_SAVE_H
#include "model.h"
namespace sg {
bool save_game(const Game& game);
bool save_game(const Game& game, int slot);
bool load_game(Game& game);
bool load_game_slot(Game& game, int slot);
bool game_pass_unlocked();
bool unlock_game_pass();
bool save_slot_info(int slot, uint8_t& scenario, uint8_t& player, uint8_t& cities, uint8_t& generals,
                    uint16_t& year, uint8_t& month, uint32_t& sequence);
}
#endif
