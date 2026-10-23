#ifndef SANGO_AUDIO_H
#define SANGO_AUDIO_H
#include <stdint.h>
namespace sg::audio {
enum class Scene : uint8_t { menu, world, battle, ending };
enum class Cue : uint8_t { move, accept, back, magic, hit };
void init();
void update(Scene scene, int elapsed_frames);
void play(Cue cue);
int volume();
void set_volume(int value);
bool music_enabled();
bool effects_enabled();
void set_music_enabled(bool enabled);
void set_effects_enabled(bool enabled);
}
#endif
