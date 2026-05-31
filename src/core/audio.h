//
// audio.h - sound/music infrastructure. Works with zero assets present.
//
#ifndef AUDIO_H
#define AUDIO_H

#include "config.h"

typedef enum SoundId {
    SFX_PLACE,   // placing material
    SFX_IGNITE,  // combustion
    SFX_UI,      // menu click
    SFX_COUNT,
} SoundId;

void Audio_Init(const AppConfig *cfg); // opens device, loads any present files
void Audio_Play(SoundId id);            // no-op if that sound wasn't loaded
void Audio_Update(void);                // feed the music stream
void Audio_SetVolumes(float master, float sfx, float music);
void Audio_Close(void);

#endif // AUDIO_H
