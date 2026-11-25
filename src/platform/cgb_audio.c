// Pokémon World web/PC build: the GBA's Game Boy sound channels (two square waves, the wave
// channel and noise), played from the sound registers that CgbSound in src/m4a.c writes.
// From pokeemerald-multiplatform (MIT, see PROJECT_CREDITS.md), adapted for pokeemerald-expansion.
//
// Unlike pokeemerald-multiplatform's version, src/m4a.c needs no hooks: once per frame (after
// CgbSound) this looks at the registers. A write with the trigger bit (bit 7 of NRx4) starts the
// channel again and the bit is cleared here (on hardware it can't be read back, and CgbSound
// never reads it). Between frames the hardware's frame sequencer runs: length counters (256 Hz),
// ch1's frequency sweep (128 Hz) and the volume envelopes (64 Hz), which CgbSound relies on.
// Each output sample averages 4 sub-samples, which keeps the square waves from aliasing badly.
// Levels follow the GBA's mixer: 4-bit channel outputs times the NR50 master volume times the
// SOUNDCNT_H ratio, against ±512 for full-scale Direct Sound.

#ifdef PORTABLE

#include "global.h"
#include "platform/sound.h"

#define OVERSAMPLE 4
#define SUBRATE (PLATFORM_SOUND_RATE * OVERSAMPLE)

struct CgbState
{
    bool8 on[4];
    u8 volume[4];       // envelope volume, 0-15 (channels 1, 2 and 4)
    u8 envTimer[4];
    u16 length[4];      // length counter (counts down while NRx4 bit 6 is set)
    u8 lastLengthReg[4];
    u16 sweepFreq;      // channel 1's sweep "shadow" frequency
    u8 sweepTimer;
    bool8 sweepOn;
    float phase[3];     // square: duty step 0-8, wave: sample 0-32
    u16 lfsr;
    float noiseClock;
    float sequencer;    // frame sequencer, in 1/512 s
    u8 sequencerStep;
};

static struct CgbState sCgb = { .lfsr = 0x7FFF };

static const u8 sDuty[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
    {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
    {0, 1, 1, 1, 1, 1, 1, 0}, // 75%
};
static const float sDutyMean[4] = {1 / 8.0f, 2 / 8.0f, 4 / 8.0f, 6 / 8.0f};

static const u8 sNRx1Offsets[4] = {REG_OFFSET_NR11, REG_OFFSET_NR21, REG_OFFSET_NR31, REG_OFFSET_NR41};
static const u8 sNRx2Offsets[4] = {REG_OFFSET_NR12, REG_OFFSET_NR22, REG_OFFSET_NR32, REG_OFFSET_NR42};
static const u8 sNRx4Offsets[4] = {REG_OFFSET_NR14, REG_OFFSET_NR24, REG_OFFSET_NR34, REG_OFFSET_NR44};
#define NRX1(ch) (*(vu8 *)(REG_BASE + sNRx1Offsets[ch]))
#define NRX2(ch) (*(vu8 *)(REG_BASE + sNRx2Offsets[ch]))
#define NRX4(ch) (*(vu8 *)(REG_BASE + sNRx4Offsets[ch]))

static bool32 DacOn(u32 ch)
{
    if (ch == 2)
        return (REG_NR30 & 0x80) != 0;
    return (NRX2(ch) & 0xF8) != 0;
}

static u32 Ch1Freq(void)
{
    return REG_NR13 | ((REG_NR14 & 7) << 8);
}

static u32 SweepNext(void)
{
    u32 delta = sCgb.sweepFreq >> (REG_NR10 & 7);
    return (REG_NR10 & 0x08) ? sCgb.sweepFreq - delta : sCgb.sweepFreq + delta;
}

static void Trigger(u32 ch)
{
    u8 nrx1 = NRX1(ch), nrx2 = NRX2(ch);
    u32 maxLength = ch == 2 ? 256 : 64;

    // The length counter is loaded when NRx1 is written; CgbSound writes it at the start of a
    // note, together with a trigger, and retriggers later (on volume changes) without writing it.
    if (sCgb.length[ch] == 0 || nrx1 != sCgb.lastLengthReg[ch])
        sCgb.length[ch] = maxLength - (ch == 2 ? nrx1 : (nrx1 & 0x3F));
    sCgb.lastLengthReg[ch] = nrx1;

    sCgb.on[ch] = DacOn(ch);
    if (ch != 2)
    {
        sCgb.volume[ch] = nrx2 >> 4;
        sCgb.envTimer[ch] = nrx2 & 7;
    }
    if (ch == 0)
    {
        u32 period = (REG_NR10 >> 4) & 7;
        sCgb.sweepFreq = Ch1Freq();
        sCgb.sweepTimer = period ? period : 8;
        sCgb.sweepOn = period != 0 || (REG_NR10 & 7) != 0;
        if ((REG_NR10 & 7) && SweepNext() > 2047)
            sCgb.on[0] = FALSE;
    }
    else if (ch == 2)
    {
        sCgb.phase[2] = 0;
    }
    else if (ch == 3)
    {
        sCgb.lfsr = 0x7FFF;
    }
}

static void ClockSequencer(void)
{
    u32 step = sCgb.sequencerStep++ & 7, ch;

    if ((step & 1) == 0) // length, 256 Hz
    {
        for (ch = 0; ch < 4; ch++)
        {
            if ((NRX4(ch) & 0x40) && sCgb.length[ch] != 0 && --sCgb.length[ch] == 0)
                sCgb.on[ch] = FALSE;
        }
    }
    if ((step == 2 || step == 6) && sCgb.sweepOn && --sCgb.sweepTimer == 0) // sweep, 128 Hz
    {
        u32 period = (REG_NR10 >> 4) & 7;
        sCgb.sweepTimer = period ? period : 8;
        if (period != 0)
        {
            u32 freq = SweepNext();
            if (freq > 2047)
            {
                sCgb.on[0] = FALSE;
            }
            else if (REG_NR10 & 7)
            {
                sCgb.sweepFreq = freq;
                REG_NR13 = freq & 0xFF;
                REG_NR14 = (REG_NR14 & ~7) | ((freq >> 8) & 7);
                if (SweepNext() > 2047)
                    sCgb.on[0] = FALSE;
            }
        }
    }
    if (step == 7) // envelopes, 64 Hz
    {
        for (ch = 0; ch < 4; ch++)
        {
            u8 nrx2;
            if (ch == 2)
                continue;
            nrx2 = NRX2(ch);
            if ((nrx2 & 7) == 0 || --sCgb.envTimer[ch] != 0)
                continue;
            sCgb.envTimer[ch] = nrx2 & 7;
            if ((nrx2 & 0x08) && sCgb.volume[ch] < 15)
                sCgb.volume[ch]++;
            else if (!(nrx2 & 0x08) && sCgb.volume[ch] > 0)
                sCgb.volume[ch]--;
        }
    }
}

void PlatformCgb_Render(float *out, u32 numSamples)
{
    u32 ch, i, k;
    float squareStep[2], waveStep, noiseStep, waveVolume, gainL, gainR;
    u32 duty[2];
    u8 nr51, nr43;
    static const float sWaveVolume[4] = {0.0f, 1.0f, 0.5f, 0.25f};
    static const float sPsgRatio[4] = {0.25f, 0.5f, 1.0f, 1.0f};
    s32 wave[32];

    for (ch = 0; ch < 4; ch++)
    {
        if (NRX4(ch) & 0x80)
        {
            NRX4(ch) &= 0x7F;
            Trigger(ch);
        }
        if (!DacOn(ch))
            sCgb.on[ch] = FALSE;
    }
    gPlatformSoundDebug.activeCgbChannels = sCgb.on[0] + sCgb.on[1] + sCgb.on[2] + sCgb.on[3];
    if (!(REG_NR52 & SOUND_MASTER_ENABLE))
        return;

    // The registers don't change during a frame, except for what the sweep writes.
    duty[0] = REG_NR11 >> 6;
    duty[1] = REG_NR21 >> 6;
    squareStep[1] = 8 * 131072.0f / (2048 - (REG_NR23 | ((REG_NR24 & 7) << 8))) / SUBRATE;
    waveStep = 2097152.0f / (2048 - (REG_NR33 | ((REG_NR34 & 7) << 8))) / SUBRATE;
    waveVolume = (REG_NR32 & 0x80) ? 0.75f : sWaveVolume[(REG_NR32 >> 5) & 3];
    for (i = 0; i < 16; i++)
    {
        u8 b = ((vu8 *)REG_ADDR_WAVE_RAM0)[i];
        wave[i * 2] = b >> 4;
        wave[i * 2 + 1] = b & 0xF;
    }
    nr43 = REG_NR43;
    {
        u32 r = nr43 & 7, s = nr43 >> 4;
        noiseStep = (r ? 524288.0f / r : 1048576.0f) / (float)(2u << s) / SUBRATE;
    }
    nr51 = REG_NR51;
    // ×8 per NR50 step, then ÷4 at the 100% ratio (as mGBA does it); 512 is full scale
    gainL = (1 + ((REG_NR50 >> 4) & 7)) * 2.0f * sPsgRatio[REG_SOUNDCNT_H & 3] / 512.0f / OVERSAMPLE;
    gainR = (1 + (REG_NR50 & 7)) * 2.0f * sPsgRatio[REG_SOUNDCNT_H & 3] / 512.0f / OVERSAMPLE;

    if (!sCgb.on[0] && !sCgb.on[1] && !sCgb.on[2] && !sCgb.on[3])
    {
        // keep the frame sequencer going (length counters)
        sCgb.sequencer += numSamples * 512.0f / PLATFORM_SOUND_RATE;
        while (sCgb.sequencer >= 1.0f)
        {
            sCgb.sequencer -= 1.0f;
            ClockSequencer();
        }
        return;
    }

    for (i = 0; i < numSamples; i++)
    {
        float l = 0, r = 0;

        sCgb.sequencer += 512.0f / PLATFORM_SOUND_RATE;
        if (sCgb.sequencer >= 1.0f)
        {
            sCgb.sequencer -= 1.0f;
            ClockSequencer();
        }
        squareStep[0] = 8 * 131072.0f / (2048 - Ch1Freq()) / SUBRATE; // the sweep changes it

        for (k = 0; k < OVERSAMPLE; k++)
        {
            float v[4] = {0, 0, 0, 0};

            for (ch = 0; ch < 2; ch++)
            {
                if (!sCgb.on[ch])
                    continue;
                sCgb.phase[ch] += squareStep[ch];
                while (sCgb.phase[ch] >= 8.0f)
                    sCgb.phase[ch] -= 8.0f;
                v[ch] = (sDuty[duty[ch]][(u32)sCgb.phase[ch] & 7] - sDutyMean[duty[ch]]) * sCgb.volume[ch];
            }
            if (sCgb.on[2])
            {
                sCgb.phase[2] += waveStep;
                while (sCgb.phase[2] >= 32.0f)
                    sCgb.phase[2] -= 32.0f;
                v[2] = (wave[(u32)sCgb.phase[2] & 31] - 7.5f) * waveVolume;
            }
            if (sCgb.on[3])
            {
                u32 clocks;
                sCgb.noiseClock += noiseStep;
                for (clocks = 0; sCgb.noiseClock >= 1.0f && clocks < 64; clocks++)
                {
                    u32 bit = (sCgb.lfsr ^ (sCgb.lfsr >> 1)) & 1;
                    sCgb.noiseClock -= 1.0f;
                    sCgb.lfsr = (sCgb.lfsr >> 1) | (bit << 14);
                    if (nr43 & 0x08)
                        sCgb.lfsr = (sCgb.lfsr & ~0x40) | (bit << 6);
                }
                if (sCgb.noiseClock >= 1.0f)
                    sCgb.noiseClock -= (u32)sCgb.noiseClock;
                v[3] = ((~sCgb.lfsr & 1) - 0.5f) * sCgb.volume[3];
            }

            for (ch = 0; ch < 4; ch++)
            {
                if (nr51 & (0x10 << ch))
                    l += v[ch];
                if (nr51 & (1 << ch))
                    r += v[ch];
            }
        }
        out[i * 2] += l * gainL;
        out[i * 2 + 1] += r * gainR;
    }
}

#endif // PORTABLE
