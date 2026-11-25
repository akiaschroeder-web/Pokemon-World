// Pokémon World web/PC build: the sound engine's platform side (src/platform/m4a_engine.c,
// sound_mixer.c and cgb_audio.c), which stands in for the GBA's assembly sound driver
// (src/m4a_1.s) and its sound hardware.
#ifndef GUARD_PLATFORM_SOUND_H
#define GUARD_PLATFORM_SOUND_H

#include "gba/m4a_internal.h"

// The output: stereo float samples (left, right, left, ...) at a fixed rate, whatever rate the
// game picks for its mixer (it uses 13379 Hz; mixing at a higher rate here sounds cleaner and
// keeps the same pitches and timing). One V-blank is exactly PLATFORM_SOUND_SAMPLES_PER_FRAME.
#define PLATFORM_SOUND_SAMPLES_PER_FRAME 704
#define PLATFORM_SOUND_FRAME_RATE 59.7275f
#define PLATFORM_SOUND_RATE ((float)PLATFORM_SOUND_SAMPLES_PER_FRAME * PLATFORM_SOUND_FRAME_RATE) // 42048 Hz

// sound_mixer.c: mixes one frame (the Direct Sound channels plus the Game Boy channels) and
// queues it. Called by SoundMain once per V-blank, after the sequencer and CgbSound have run.
void PlatformSound_MixFrame(struct SoundInfo *soundInfo);
// Hands over what was mixed since the last call: returns the number of stereo samples (0 if the
// sound driver didn't run) and points *samples at them. The buffer stays valid until the next mix.
u32 PlatformSound_TakeQueued(const float **samples);

// cgb_audio.c: the Game Boy sound channels 1-4, driven by the sound registers (REG_NR10...)
// that CgbSound in src/m4a.c writes. Adds one frame of PSG output to out (stereo, interleaved).
void PlatformCgb_Render(float *out, u32 numSamples);

// For testing: what was playing in the last frame mixed.
struct PlatformSoundDebug
{
    u32 frames;             // frames mixed so far
    u32 cryFrames;          // frames in which a cry (a reversed or compressed sample) played
    float cryRms;           // the cry channels' loudness in the last frame
    u32 activeChannels;     // Direct Sound channels on in the last frame
    u32 activeCgbChannels;  // Game Boy channels on in the last frame
};
extern struct PlatformSoundDebug gPlatformSoundDebug;

#endif // GUARD_PLATFORM_SOUND_H
