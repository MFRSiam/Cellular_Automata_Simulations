//
// audio.c - thin wrapper over raylib audio. Every load is optional: a missing
// file just leaves that slot silent, so the app runs fine with no audio pack.
//
#include "audio.h"

#include <string.h>

// Map each sound id to a file under assets/audio/.
static const char *SOUND_FILES[SFX_COUNT] = {
    [SFX_PLACE]  = "assets/audio/place.wav",
    [SFX_IGNITE] = "assets/audio/ignite.wav",
    [SFX_UI]     = "assets/audio/ui.wav",
};

static struct {
    bool  ready;
    Sound sounds[SFX_COUNT];
    bool  loaded[SFX_COUNT];
    Music music;
    bool  hasMusic;
    float sfxVol;
} A;

void Audio_Init(const AppConfig *cfg) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "AUDIO: device not ready, running silent");
        return;
    }
    A.ready = true;
    A.sfxVol = cfg->volSfx;
    SetMasterVolume(cfg->volMaster);

    for (int i = 0; i < SFX_COUNT; i++) {
        const char *path = AssetPath(SOUND_FILES[i]);
        if (FileExists(path)) { A.sounds[i] = LoadSound(path); A.loaded[i] = true; }
    }

    const char *musicPath = AssetPath("assets/audio/music.ogg");
    if (FileExists(musicPath)) {
        A.music = LoadMusicStream(musicPath);
        A.hasMusic = true;
        SetMusicVolume(A.music, cfg->volMusic);
        PlayMusicStream(A.music);
    }
}

void Audio_Play(SoundId id) {
    if (!A.ready || id < 0 || id >= SFX_COUNT || !A.loaded[id]) return;
    SetSoundVolume(A.sounds[id], A.sfxVol);
    PlaySound(A.sounds[id]);
}

void Audio_Update(void) {
    if (A.ready && A.hasMusic) UpdateMusicStream(A.music);
}

void Audio_SetVolumes(float master, float sfx, float music) {
    if (!A.ready) return;
    SetMasterVolume(master);
    A.sfxVol = sfx;
    if (A.hasMusic) SetMusicVolume(A.music, music);
}

void Audio_Close(void) {
    if (!A.ready) return;
    for (int i = 0; i < SFX_COUNT; i++) if (A.loaded[i]) UnloadSound(A.sounds[i]);
    if (A.hasMusic) UnloadMusicStream(A.music);
    CloseAudioDevice();
    memset(&A, 0, sizeof A);
}
