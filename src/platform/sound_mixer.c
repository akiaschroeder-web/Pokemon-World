// Pokémon World web/PC build: the m4a sound driver's PCM ("Direct Sound") mixer in C, standing in
// for SoundMainRAM in src/m4a_1.s, plus the final mix with the Game Boy channels (cgb_audio.c).
// From pokeemerald-multiplatform (MIT, see PROJECT_CREDITS.md), adapted for pokeemerald-expansion.
//
// What it does, as on the GBA:
//  - envelopes (attack, decay, sustain, release, pseudo echo) once per frame for each channel;
//  - linear interpolation between samples, or one sample per output sample for "fixed" voices;
//  - Pokémon cries: TONEDATA_TYPE_REV plays a sample backwards, TONEDATA_TYPE_CMP plays one
//    stored compressed (expansion's cries are converted with wav2agb -c: blocks of 0x21 bytes,
//    a first sample then 63 4-bit deltas looked up in gDeltaEncodingTable, giving 64 samples);
//  - reverb: the frame is first set to the mono sum of the frames written pcmDmaPeriod and
//    pcmDmaPeriod - 1 V-blanks ago, times reverb / 512.
// What differs: it mixes in float at PLATFORM_SOUND_RATE (42048 Hz) instead of into 8-bit
// samples at the game's rate (13379 Hz), so there's no 8-bit wraparound and less aliasing.
// The struct fields keep the GBA's meanings (count, currentPointer); the fractional sample
// position is kept here in float instead of the 9.23 fixed point fw field.

#ifdef PORTABLE

#include "global.h"
#include "gba/m4a_internal.h"
#include "platform/sound.h"
#include <math.h>

#define TONEDATA_TYPE_REV 0x10 // play backwards (constants/m4a_constants.inc)
#define TONEDATA_TYPE_CMP 0x20 // compressed sample
#define SOUND_CHANNEL_SF_SPECIAL 0x20 // the reverse/compressed set-up has been done
#define WAVE_DATA_FLAG_LOOP 0xC0

#define SPF PLATFORM_SOUND_SAMPLES_PER_FRAME
#define MAX_DMA_PERIOD 16
#define MAX_QUEUED_FRAMES 4

extern const s8 gDeltaEncodingTable[];

struct MixState
{
    float fw;               // position between two samples, 0 <= fw < 1
    const struct WaveData *wav; // the sample decoded[] belongs to
    s32 block;              // which 64-sample block of a compressed sample is in decoded[]
    s8 decoded[64];
};

static struct MixState sMix[MAX_DIRECTSOUND_CHANNELS];
static float sDmaFrames[MAX_DMA_PERIOD][SPF * 2]; // the Direct Sound output of the last frames (L, R)
static u32 sDmaFrameIndex;
static float sQueue[MAX_QUEUED_FRAMES * SPF * 2];
static u32 sQueuedFrames;
static float sCryFrame[SPF * 2];
struct PlatformSoundDebug gPlatformSoundDebug;

static inline u32 WavLoopFlags(const struct WaveData *wav)
{
    return ((const u8 *)wav)[3]; // the byte at o_WaveData_flags
}

// One sample of an uncompressed wave (the GBA reads one past the end when interpolating; so do we).
static inline s32 RawSample(const struct WaveData *wav, s32 i)
{
    if (i < 0 || (u32)i > wav->size)
        return 0;
    return wav->data[i];
}

static s32 CompressedSample(struct MixState *ms, const struct WaveData *wav, s32 i)
{
    s32 block = i >> 6;
    if (i < 0 || (u32)block >= (wav->size + 63) / 64)
        return 0;
    if (ms->wav != wav || ms->block != block)
    {
        const u8 *src = (const u8 *)wav->data + block * 0x21;
        s32 j, s = (s8)*src++;
        ms->decoded[0] = s;
        s = (s8)(s + gDeltaEncodingTable[*src++ & 0xF]);
        ms->decoded[1] = s;
        for (j = 2; j < 64; j += 2)
        {
            u8 deltas = *src++;
            s = (s8)(s + gDeltaEncodingTable[deltas >> 4]);
            ms->decoded[j] = s;
            s = (s8)(s + gDeltaEncodingTable[deltas & 0xF]);
            ms->decoded[j + 1] = s;
        }
        ms->wav = wav;
        ms->block = block;
    }
    return ms->decoded[i & 63];
}

static inline s32 Sample(struct MixState *ms, const struct WaveData *wav, bool32 compressed, s32 i)
{
    return compressed ? CompressedSample(ms, wav, i) : RawSample(wav, i);
}

// Plays forwards from sample *pos; *count is how many samples are left (to the end of the wave).
// Returns FALSE when the sample ended (the channel is then off).
static bool32 MixForward(struct MixState *ms, const struct WaveData *wav, bool32 compressed, s32 *pos, s32 *count,
                         float step, s32 loopStart, s32 loopLen, float *out, float gainL, float gainR)
{
    s32 p = *pos, n = *count, i;
    float fw = ms->fw;
    float b = Sample(ms, wav, compressed, p);
    float m = Sample(ms, wav, compressed, p + 1) - b;

    for (i = 0; i < SPF; i++)
    {
        float s = b + fw * m;
        out[i * 2] += s * gainL;
        out[i * 2 + 1] += s * gainR;
        fw += step;
        if (fw >= 1.0f)
        {
            s32 adv = (s32)fw;
            fw -= adv;
            n -= adv;
            if (n <= 0)
            {
                s32 over;
                if (loopLen <= 0)
                    return FALSE;
                over = -n;
                n += loopLen;
                while (n <= 0)
                {
                    over -= loopLen;
                    n += loopLen;
                }
                p = loopStart + over;
            }
            else
            {
                p += adv;
            }
            b = Sample(ms, wav, compressed, p);
            m = Sample(ms, wav, compressed, p + 1) - b;
        }
    }
    *pos = p;
    *count = n;
    ms->fw = fw;
    return TRUE;
}

// Plays backwards; *pos is one past the next sample to play. No looping.
static bool32 MixBackward(struct MixState *ms, const struct WaveData *wav, bool32 compressed, s32 *pos, s32 *count,
                          float step, float *out, float gainL, float gainR)
{
    s32 p = *pos - 1, n = *count, i;
    float fw = ms->fw;
    float b = Sample(ms, wav, compressed, p);
    float m = Sample(ms, wav, compressed, p - 1) - b;

    for (i = 0; i < SPF; i++)
    {
        float s = b + fw * m;
        out[i * 2] += s * gainL;
        out[i * 2 + 1] += s * gainR;
        fw += step;
        if (fw >= 1.0f)
        {
            s32 adv = (s32)fw;
            fw -= adv;
            n -= adv;
            if (n <= 0)
                return FALSE;
            p -= adv;
            b = Sample(ms, wav, compressed, p);
            m = Sample(ms, wav, compressed, p - 1) - b;
        }
    }
    *pos = p + 1;
    *count = n;
    ms->fw = fw;
    return TRUE;
}

static void MixChannel(struct SoundInfo *soundInfo, struct SoundChannel *chan, struct MixState *ms, float *out)
{
    const struct WaveData *wav = chan->wav;
    u32 flags = chan->statusFlags;
    s32 env, vol, loopStart = 0, loopLen = 0, pos, count;
    float step, gainL, gainR;
    bool32 compressed, ok;

    if (!(flags & SOUND_CHANNEL_SF_ON) || wav == NULL)
        return;

    // 1. the envelope
    if (flags & SOUND_CHANNEL_SF_START)
    {
        if (flags & SOUND_CHANNEL_SF_STOP)
        {
            chan->statusFlags = 0;
            return;
        }
        flags = SOUND_CHANNEL_SF_ENV_ATTACK;
        chan->currentPointer = (s8 *)wav->data + chan->count; // count: where to start (cries)
        chan->count = wav->size - chan->count;
        ms->fw = 0;
        ms->block = -1;
        if (WavLoopFlags(wav) & WAVE_DATA_FLAG_LOOP)
            flags |= SOUND_CHANNEL_SF_LOOP;
        env = 0;
        goto attack;
    }

    env = chan->envelopeVolume;
    if (flags & SOUND_CHANNEL_SF_IEC)
    {
        // pseudo echo: constant volume for pseudoEchoLength frames
        if (chan->pseudoEchoLength-- == 1)
        {
            chan->statusFlags = 0;
            return;
        }
    }
    else if (flags & SOUND_CHANNEL_SF_STOP)
    {
        env = (env * chan->release) >> 8;
        if (env <= chan->pseudoEchoVolume)
        {
        echo:
            env = chan->pseudoEchoVolume;
            if (env == 0)
            {
                chan->statusFlags = 0;
                return;
            }
            flags |= SOUND_CHANNEL_SF_IEC;
        }
    }
    else if ((flags & SOUND_CHANNEL_SF_ENV) == SOUND_CHANNEL_SF_ENV_DECAY)
    {
        env = (env * chan->decay) >> 8;
        if (env <= chan->sustain)
        {
            env = chan->sustain;
            if (env == 0)
                goto echo;
            flags--;
        }
    }
    else if ((flags & SOUND_CHANNEL_SF_ENV) == SOUND_CHANNEL_SF_ENV_ATTACK)
    {
    attack:
        env += chan->attack;
        if (env >= 0xFF)
        {
            env = 0xFF;
            flags--;
        }
    }
    chan->statusFlags = flags;
    chan->envelopeVolume = env;

    vol = (env * (soundInfo->masterVolume + 1)) >> 4;
    chan->envelopeVolumeRight = (chan->rightVolume * vol) >> 8;
    chan->envelopeVolumeLeft = (chan->leftVolume * vol) >> 8;
    // the GBA adds sample * volume / 256 to 8-bit output samples; 1.0 here is 128 there
    gainR = chan->envelopeVolumeRight / 32768.0f;
    gainL = chan->envelopeVolumeLeft / 32768.0f;

    if (flags & SOUND_CHANNEL_SF_LOOP)
    {
        loopStart = wav->loopStart;
        loopLen = wav->size - wav->loopStart;
    }

    // 2. the samples
    if (chan->type & TONEDATA_TYPE_FIX)
        step = soundInfo->pcmSamplesPerVBlank / (float)SPF; // one sample per sample at the game's rate
    else
        step = chan->frequency / PLATFORM_SOUND_RATE;
    count = chan->count;

    if (chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV))
    {
        compressed = wav->type != 0;
        if (!(chan->statusFlags & SOUND_CHANNEL_SF_SPECIAL))
        {
            chan->statusFlags |= SOUND_CHANNEL_SF_SPECIAL;
            if (chan->type & TONEDATA_TYPE_REV)
                chan->currentPointer = (s8 *)wav->data + wav->size - (chan->currentPointer - wav->data);
            if (compressed)
                chan->currentPointer = (s8 *)(uintptr_t)(chan->currentPointer - wav->data); // a sample index from now on
        }
        pos = compressed ? (s32)(uintptr_t)chan->currentPointer : chan->currentPointer - wav->data;
        if (chan->type & TONEDATA_TYPE_REV)
            ok = MixBackward(ms, wav, compressed, &pos, &count, step, out, gainL, gainR);
        else if (compressed)
            ok = MixForward(ms, wav, TRUE, &pos, &count, step, loopStart, loopLen, out, gainL, gainR);
        else
            return; // TONEDATA_TYPE_CMP with an uncompressed wave: the GBA plays nothing
        if (!ok)
        {
            chan->statusFlags = 0;
            return;
        }
        chan->currentPointer = compressed ? (s8 *)(uintptr_t)pos : (s8 *)wav->data + pos;
    }
    else
    {
        pos = chan->currentPointer - wav->data;
        if (!MixForward(ms, wav, FALSE, &pos, &count, step, loopStart, loopLen, out, gainL, gainR))
        {
            chan->statusFlags = 0;
            return;
        }
        chan->currentPointer = (s8 *)wav->data + pos;
    }
    chan->count = count;
}

// The Pokédex cry screen draws the waveform from pcmBuffer, so keep a copy there at the game's
// rate, where the GBA's mixer would have put it.
static void UpdatePcmBuffer(struct SoundInfo *soundInfo, const float *frame)
{
    s32 spv = soundInfo->pcmSamplesPerVBlank, offset = 0, i;
    s32 counter = soundInfo->pcmDmaCounter;

    if (spv <= 0 || spv > PCM_DMA_BUF_SIZE)
        return;
    if (counter - 1 > 0)
        offset = spv * (soundInfo->pcmDmaPeriod - (counter - 1));
    if (offset < 0 || offset + spv > PCM_DMA_BUF_SIZE)
        return;
    for (i = 0; i < spv; i++)
    {
        s32 j = i * SPF / spv;
        s32 r = (s32)(frame[j * 2 + 1] * 128.0f), l = (s32)(frame[j * 2] * 128.0f);
        soundInfo->pcmBuffer[offset + i] = r < -128 ? -128 : r > 127 ? 127 : r;
        soundInfo->pcmBuffer[PCM_DMA_BUF_SIZE + offset + i] = l < -128 ? -128 : l > 127 ? 127 : l;
    }
}

void PlatformSound_MixFrame(struct SoundInfo *soundInfo)
{
    u32 period = soundInfo->pcmDmaPeriod, i;
    float *frame, *older, *out;
    u16 soundcnt = REG_SOUNDCNT_H;
    float gainA, gainB;

    if (period < 1)
        period = 1;
    if (period > MAX_DMA_PERIOD)
        period = MAX_DMA_PERIOD;
    frame = sDmaFrames[sDmaFrameIndex % period];
    older = sDmaFrames[(sDmaFrameIndex + 1) % period];
    sDmaFrameIndex++;

    if (soundInfo->reverb != 0)
    {
        float k = soundInfo->reverb / 512.0f;
        for (i = 0; i < SPF; i++)
        {
            float s = (frame[i * 2] + frame[i * 2 + 1] + older[i * 2] + older[i * 2 + 1]) * k;
            if (s > -1e-6f && s < 1e-6f)
                s = 0; // let the echo die out instead of going on in tiny (slow) denormal numbers
            frame[i * 2] = frame[i * 2 + 1] = s;
        }
    }
    else
    {
        memset(frame, 0, sizeof(sDmaFrames[0]));
    }

    {
        float cryEnergy = 0;
        bool32 cry = FALSE;

        gPlatformSoundDebug.activeChannels = 0;
        for (i = 0; i < soundInfo->maxChans && i < MAX_DIRECTSOUND_CHANNELS; i++)
        {
            struct SoundChannel *chan = &soundInfo->chans[i];
            if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
                continue;
            gPlatformSoundDebug.activeChannels++;
            if (chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV))
            {
                // a cry: mixed on its own first so that tests can tell it's there
                u32 j;
                memset(sCryFrame, 0, sizeof(sCryFrame));
                MixChannel(soundInfo, chan, &sMix[i], sCryFrame);
                for (j = 0; j < SPF * 2; j++)
                {
                    cryEnergy += sCryFrame[j] * sCryFrame[j];
                    frame[j] += sCryFrame[j];
                }
                cry = TRUE;
            }
            else
            {
                MixChannel(soundInfo, chan, &sMix[i], frame);
            }
        }
        gPlatformSoundDebug.frames++;
        gPlatformSoundDebug.cryFrames += cry;
        gPlatformSoundDebug.cryRms = sqrtf(cryEnergy / (SPF * 2));
    }

    UpdatePcmBuffer(soundInfo, frame);

    // The final mix, as SOUNDCNT_H routes it: FIFO A has the right channel, FIFO B the left one
    // (or both have everything, at half volume, when cries are set to mono).
    if (sQueuedFrames >= MAX_QUEUED_FRAMES)
        sQueuedFrames = 0; // nobody took them
    out = &sQueue[sQueuedFrames * SPF * 2];
    sQueuedFrames++;
    memset(out, 0, SPF * 2 * sizeof(float));
    if (!(REG_NR52 & SOUND_MASTER_ENABLE))
        return;

    gainA = (soundcnt & SOUND_A_MIX_FULL) ? 1.0f : 0.5f;
    gainB = (soundcnt & SOUND_B_MIX_FULL) ? 1.0f : 0.5f;
    for (i = 0; i < SPF; i++)
    {
        float a = frame[i * 2 + 1] * gainA, b = frame[i * 2] * gainB;
        if (soundcnt & SOUND_A_LEFT_OUTPUT)
            out[i * 2] += a;
        if (soundcnt & SOUND_A_RIGHT_OUTPUT)
            out[i * 2 + 1] += a;
        if (soundcnt & SOUND_B_LEFT_OUTPUT)
            out[i * 2] += b;
        if (soundcnt & SOUND_B_RIGHT_OUTPUT)
            out[i * 2 + 1] += b;
    }

    PlatformCgb_Render(out, SPF);

    // The GBA's output goes through a capacitor, which removes any DC offset (the Game Boy
    // channels have one): a high-pass filter at about 10 Hz.
    for (i = 0; i < SPF * 2; i++)
    {
        static float sLastIn[2], sLastOut[2];
        u32 c = i & 1;
        float x = out[i], y = x - sLastIn[c] + 0.9985f * sLastOut[c];
        if (y > -1e-9f && y < 1e-9f)
            y = 0;
        sLastIn[c] = x;
        sLastOut[c] = y;
        out[i] = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
    }
}

u32 PlatformSound_TakeQueued(const float **samples)
{
    u32 n = sQueuedFrames * SPF;
    *samples = sQueue;
    sQueuedFrames = 0;
    return n;
}

#endif // PORTABLE
