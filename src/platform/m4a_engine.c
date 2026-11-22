// Pokémon World web/PC build: the m4a (MusicPlayer2000) sound driver's main loop and sequencer
// in C, standing in for the GBA assembly in src/m4a_1.s. The PCM mixer is in sound_mixer.c and
// the Game Boy channels in cgb_audio.c; the rest of the driver is the shared C in src/m4a.c.
// From pokeemerald-multiplatform (MIT, see PROJECT_CREDITS.md), adapted for pokeemerald-expansion.
//
// Unlike pokeemerald-multiplatform, this works on expansion's own structs
// (include/gba/m4a_internal.h, unchanged), and follows expansion's m4a_1.s line by line
// (including the cry "progress" offset that ply_note copies from the track).
//
// WebAssembly traps on an indirect call whose signature doesn't match exactly, so every function
// that is stored in a table here has exactly the signature it is called with:
//  - gMPlayJumpTable[0..29] (sequencer commands): (struct MusicPlayerInfo *, struct MusicPlayerTrack *)
//  - gMPlayJumpTable[34], [35] (ClearChain, Clear64byte in m4a.c): (void *)
//  - soundInfo->plynote: ply_note(u32, struct MusicPlayerInfo *, struct MusicPlayerTrack *)

#ifdef PORTABLE

#include "global.h"
#include "gba/m4a_internal.h"
#include "platform/sound.h"

extern const u8 gClockTable[];
u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust); // src/m4a.c

u32 umul3232H32(u32 a, u32 b)
{
    return (u32)(((u64)a * b) >> 32);
}

void SoundMainBTM(void)
{
}

// The BIOS's 64-byte clear (jump table entry 35, called as Clear64byte(void *)).
static void Clear64Bytes(void *x)
{
    memset(x, 0, 64);
}

// Removes a channel from its track's list of channels (jump table entry 34).
void RealClearChain(void *x)
{
    struct SoundChannel *chan = x;
    struct MusicPlayerTrack *track = chan->track;
    struct SoundChannel *next, *prev;

    if (track == NULL)
        return;
    next = chan->nextChannelPointer;
    prev = chan->prevChannelPointer;
    if (prev != NULL)
        prev->nextChannelPointer = next;
    else
        track->chan = next;
    if (next != NULL)
        next->prevChannelPointer = prev;
    chan->track = NULL;
}

static inline u8 ReadByte(struct MusicPlayerTrack *track)
{
    return *track->cmdPtr++;
}

// --- sequencer commands (0xB1-0xCE), called through gMPlayJumpTable

void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if (chan->statusFlags & SOUND_CHANNEL_SF_ON)
            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
        RealClearChain(chan); // leaves chan->nextChannelPointer as it is
    }
    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    const u8 *p = track->cmdPtr;
    track->cmdPtr = (u8 *)(uintptr_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24));
}

void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    if (track->patternLevel < 3)
    {
        track->patternStack[track->patternLevel] = track->cmdPtr + 4;
        track->patternLevel++;
        ply_goto(mplayInfo, track);
    }
    else
    {
        ply_fine(mplayInfo, track);
    }
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    if (track->patternLevel != 0)
    {
        track->patternLevel--;
        track->cmdPtr = track->patternStack[track->patternLevel];
    }
}

void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 *countPtr = track->cmdPtr;

    if (*countPtr == 0)
    {
        // repeat forever
        track->cmdPtr++;
        ply_goto(mplayInfo, track);
        return;
    }
    track->repN++;
    track->cmdPtr++;
    if (track->repN < *countPtr)
    {
        ply_goto(mplayInfo, track);
    }
    else
    {
        track->repN = 0;
        track->cmdPtr = countPtr + 5;
    }
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->priority = ReadByte(track);
}

void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 tempo = ReadByte(track) * 2;
    mplayInfo->tempoD = tempo;
    mplayInfo->tempoI = (tempo * mplayInfo->tempoU) >> 8;
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->keyShift = ReadByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 voice = ReadByte(track);
    track->tone = mplayInfo->tone[voice];
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->vol = ReadByte(track);
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->pan = ReadByte(track) - C_V;
    track->flags |= MPT_FLG_VOLCHG;
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->bend = ReadByte(track) - C_V;
    track->flags |= MPT_FLG_PITCHG;
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->bendRange = ReadByte(track);
    track->flags |= MPT_FLG_PITCHG;
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->lfoDelay = ReadByte(track);
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 type = ReadByte(track);
    if (type != track->modT)
    {
        track->modT = type;
        track->flags |= MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    }
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->tune = ReadByte(track) - C_V;
    track->flags |= MPT_FLG_PITCHG;
}

// Writes a Game Boy sound register directly.
void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 reg = ReadByte(track);
    u8 value = ReadByte(track);
    *(vu8 *)(REG_ADDR_NR10 + reg) = value;
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->lfoSpeed = ReadByte(track);
    if (track->lfoSpeed == 0)
        ClearModM(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->mod = ReadByte(track);
    if (track->mod == 0)
        ClearModM(track);
}

// End of a tied note: release the channel playing that key.
void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;
    u8 key = *track->cmdPtr;

    if (key < 0x80)
    {
        track->key = key;
        track->cmdPtr++;
    }
    else
    {
        key = track->key;
    }

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if ((chan->statusFlags & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV))
         && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP)
         && chan->midiKey == key)
        {
            chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
            return;
        }
    }
}

// The BIOS copies its table of sequencer commands here (m4a.c calls entries 34 and 35 directly,
// and MPlayExtender replaces a few entries). Same order as gMPlayJumpTableTemplate.
void MPlayJumpTableCopy(MPlayFunc *table)
{
    static const MPlayFunc sTemplate[36] = {
        (MPlayFunc)ply_fine, (MPlayFunc)ply_goto, (MPlayFunc)ply_patt, (MPlayFunc)ply_pend,
        (MPlayFunc)ply_rept, (MPlayFunc)ply_fine, (MPlayFunc)ply_fine, (MPlayFunc)ply_fine,
        (MPlayFunc)ply_fine, (MPlayFunc)ply_prio, (MPlayFunc)ply_tempo, (MPlayFunc)ply_keysh,
        (MPlayFunc)ply_voice, (MPlayFunc)ply_vol, (MPlayFunc)ply_pan, (MPlayFunc)ply_bend,
        (MPlayFunc)ply_bendr, (MPlayFunc)ply_lfos, (MPlayFunc)ply_lfodl, (MPlayFunc)ply_mod,
        (MPlayFunc)ply_modt, (MPlayFunc)ply_fine, (MPlayFunc)ply_fine, (MPlayFunc)ply_tune,
        (MPlayFunc)ply_fine, (MPlayFunc)ply_fine, (MPlayFunc)ply_fine, (MPlayFunc)ply_port,
        (MPlayFunc)ply_fine, (MPlayFunc)ply_endtie, (MPlayFunc)SampleFreqSet, (MPlayFunc)TrackStop,
        (MPlayFunc)FadeOutBody, (MPlayFunc)TrkVolPitSet, (MPlayFunc)RealClearChain, (MPlayFunc)Clear64Bytes,
    };
    memcpy(table, sTemplate, sizeof(sTemplate));
}

// --- notes and channels

static void ChnVolSet(struct SoundChannel *chan, struct MusicPlayerTrack *track)
{
    s32 pan = (s8)chan->rhythmPan;
    u32 right = ((128 + pan) * chan->velocity * track->volMR) >> 14;
    u32 left = ((127 - pan) * chan->velocity * track->volML) >> 14;

    chan->rightVolume = right > 0xFF ? 0xFF : right;
    chan->leftVolume = left > 0xFF ? 0xFF : left;
}

// A key split voice keeps a pointer to its table of key -> sub-voice where the envelope would be.
static inline const u8 *KeySplitTable(const struct ToneData *tone)
{
    const u8 *table;
    memcpy(&table, &tone->attack, sizeof(table));
    return table;
}

// soundInfo->CgbOscOff and MidiKeyToCgbFreq point at DummyFunc (with the wrong signature) until
// MPlayExtender sets them; don't call them before that.
static void CallCgbOscOff(struct SoundInfo *soundInfo, u8 chanNum)
{
    if (soundInfo->CgbOscOff != (CgbOscOffFunc)DummyFunc)
        soundInfo->CgbOscOff(chanNum);
}

static u32 CallMidiKeyToCgbFreq(struct SoundInfo *soundInfo, u8 chanNum, u8 key, u8 fineAdjust)
{
    if (soundInfo->MidiKeyToCgbFreq == (MidiKeyToCgbFreqFunc)DummyFunc)
        return 0;
    return soundInfo->MidiKeyToCgbFreq(chanNum, key, fineAdjust);
}

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundChannel *chan;

    if (!(track->flags & MPT_FLG_EXIST))
        return;

    for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
    {
        if (chan->statusFlags != 0)
        {
            u8 cgbType = chan->type & TONEDATA_TYPE_CGB;
            if (cgbType != 0)
                CallCgbOscOff(SOUND_INFO_PTR, cgbType);
            chan->statusFlags = 0;
        }
        chan->track = NULL;
    }
    track->chan = NULL;
}

// Note commands (0xCF-0xFF): noteCmd is the command minus 0xCF, an index into gClockTable.
void ply_note(u32 noteCmd, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    struct ToneData *tone;
    struct SoundChannel *chan;
    u8 toneType, key, cgbType;
    s8 rhythmPan = 0;
    u32 priority;
    s32 transposedKey;

    // A note is 1 to 4 bytes: the length (the command), then optionally the key, the velocity
    // and extra ticks for the length.
    track->gateTime = gClockTable[noteCmd];
    if (*track->cmdPtr < 0x80)
    {
        track->key = *track->cmdPtr++;
        if (*track->cmdPtr < 0x80)
        {
            track->velocity = *track->cmdPtr++;
            if (*track->cmdPtr < 0x80)
                track->gateTime += *track->cmdPtr++;
        }
    }

    tone = &track->tone;
    toneType = tone->type;
    key = track->key;
    if (toneType & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
    {
        // a drum kit or a key split: the voice group's wav field points at the sub-voices
        u8 index = track->key;
        if (toneType & TONEDATA_TYPE_SPL)
            index = KeySplitTable(tone)[track->key];
        tone = (struct ToneData *)tone->wav + index;
        if (tone->type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL))
            return;
        if (toneType & TONEDATA_TYPE_RHY)
        {
            if (tone->pan_sweep & 0x80)
                rhythmPan = (s8)((tone->pan_sweep - TONEDATA_P_S_PAN) << 1);
            key = tone->key;
        }
    }

    priority = mplayInfo->priority + track->priority;
    if (priority > 0xFF)
        priority = 0xFF;

    cgbType = tone->type & TONEDATA_TYPE_CGB;
    if (cgbType != 0)
    {
        // there is one channel for each Game Boy channel type
        if (soundInfo->cgbChans == NULL)
            return;
        chan = (struct SoundChannel *)&soundInfo->cgbChans[cgbType - 1];
        if ((chan->statusFlags & SOUND_CHANNEL_SF_ON) && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP))
        {
            if (chan->priority > priority)
                return;
            if (chan->priority == priority && chan->track < track)
                return;
        }
    }
    else
    {
        // Take a free channel, else one that is being released, else the one with the lowest
        // priority (on a tie, the one from the highest-numbered track).
        u32 p = priority;
        struct MusicPlayerTrack *t = track;
        bool32 foundStopping = FALSE;
        struct SoundChannel *c = soundInfo->chans;
        s32 i;

        chan = NULL;
        for (i = 0; i < soundInfo->maxChans; i++, c++)
        {
            u8 flags = c->statusFlags;
            if (!(flags & SOUND_CHANNEL_SF_ON))
            {
                chan = c;
                break;
            }
            if (flags & SOUND_CHANNEL_SF_STOP)
            {
                if (!foundStopping)
                {
                    foundStopping = TRUE;
                    p = c->priority;
                    t = c->track;
                    chan = c;
                    continue;
                }
            }
            else if (foundStopping)
            {
                continue;
            }

            if (c->priority < p)
            {
                p = c->priority;
                t = c->track;
                chan = c;
            }
            else if (c->priority == p)
            {
                if (c->track > t)
                {
                    t = c->track;
                    chan = c;
                }
                else if (c->track == t)
                {
                    chan = c;
                }
            }
        }
        if (chan == NULL)
            return;
    }

    ClearChain(chan);
    chan->prevChannelPointer = NULL;
    chan->nextChannelPointer = track->chan;
    if (track->chan != NULL)
        track->chan->prevChannelPointer = chan;
    track->chan = chan;
    chan->track = track;

    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay != 0)
        ClearModM(track);
    TrkVolPitSet(mplayInfo, track);

    chan->gateTime = track->gateTime;
    chan->midiKey = track->key;
    chan->velocity = track->velocity;
    chan->priority = priority;
    chan->key = key;
    chan->rhythmPan = rhythmPan;
    chan->type = tone->type;
    chan->wav = tone->wav;
    chan->attack = tone->attack;
    chan->decay = tone->decay;
    chan->sustain = tone->sustain;
    chan->release = tone->release;
    chan->pseudoEchoVolume = track->pseudoEchoVolume;
    chan->pseudoEchoLength = track->pseudoEchoLength;
    ChnVolSet(chan, track);

    transposedKey = chan->key + track->keyM;
    if (transposedKey < 0)
        transposedKey = 0;

    if (cgbType != 0)
    {
        struct CgbChannel *cgbChan = (struct CgbChannel *)chan;
        cgbChan->length = tone->length;
        if ((tone->pan_sweep & 0x80) || !(tone->pan_sweep & 0x70))
            cgbChan->sweep = 8;
        else
            cgbChan->sweep = tone->pan_sweep;
        chan->frequency = CallMidiKeyToCgbFreq(soundInfo, cgbType, transposedKey, track->pitM);
    }
    else
    {
        chan->count = track->unk_3C; // where to start in the sample (cries: SetPokemonCryProgress)
        chan->frequency = MidiKeyToFreq(chan->wav, transposedKey, track->pitM);
    }

    chan->statusFlags = SOUND_CHANNEL_SF_START;
    track->flags &= 0xF0;
}

// --- the music player

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    struct SoundInfo *soundInfo;
    s32 i;

    if (mplayInfo->ident != ID_NUMBER)
        return;
    mplayInfo->ident++;

    if (mplayInfo->MPlayMainNext != NULL)
        mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);

    if (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)
        goto done;
    soundInfo = SOUND_INFO_PTR;
    FadeOutBody(mplayInfo);
    if (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)
        goto done;

    mplayInfo->tempoC += mplayInfo->tempoI;
    while (mplayInfo->tempoC >= 150)
    {
        u32 trackBits = 0;
        u32 bit = 1;
        struct MusicPlayerTrack *track = mplayInfo->tracks;

        for (i = 0; i < mplayInfo->trackCount; i++, track++, bit <<= 1)
        {
            struct SoundChannel *chan;

            if (!(track->flags & MPT_FLG_EXIST))
                continue;
            trackBits |= bit;

            for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
            {
                if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
                    ClearChain(chan);
                else if (chan->gateTime != 0 && --chan->gateTime == 0)
                    chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
            }

            if (track->flags & MPT_FLG_START)
            {
                Clear64byte(track);
                track->flags = MPT_FLG_EXIST;
                track->bendRange = 2;
                track->volX = 64;
                track->lfoSpeed = 22;
                track->tone.type = 1;
            }

            while (track->wait == 0)
            {
                u8 cmd = *track->cmdPtr;
                if (cmd < 0x80)
                {
                    cmd = track->runningStatus;
                }
                else
                {
                    track->cmdPtr++;
                    if (cmd >= 0xBD)
                        track->runningStatus = cmd;
                }

                if (cmd >= 0xCF)
                {
                    soundInfo->plynote(cmd - 0xCF, mplayInfo, track);
                }
                else if (cmd > 0xB0)
                {
                    mplayInfo->cmd = cmd - 0xB1;
                    ((XcmdFunc)soundInfo->MPlayJumpTable[mplayInfo->cmd])(mplayInfo, track);
                    if (track->flags == 0)
                        goto next_track;
                }
                else
                {
                    track->wait = gClockTable[cmd - 0x80];
                }
            }

            track->wait--;

            if (track->lfoSpeed != 0 && track->mod != 0)
            {
                s32 lfo;
                if (track->lfoDelayC != 0)
                {
                    track->lfoDelayC--;
                    goto next_track;
                }
                track->lfoSpeedC += track->lfoSpeed;
                if ((s8)(track->lfoSpeedC - 0x40) < 0)
                    lfo = (s8)track->lfoSpeedC;
                else
                    lfo = 0x80 - track->lfoSpeedC;
                lfo = (track->mod * lfo) >> 6;
                if ((s8)lfo != track->modM)
                {
                    track->modM = lfo;
                    if (track->modT == 0)
                        track->flags |= MPT_FLG_PITCHG;
                    else
                        track->flags |= MPT_FLG_VOLCHG;
                }
            }
        next_track:;
        }

        mplayInfo->clock++;
        if (trackBits == 0)
        {
            mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
            goto done;
        }
        mplayInfo->status = trackBits;
        mplayInfo->tempoC -= 150;
    }

    // apply volume and pitch changes to the channels that are playing
    {
        struct MusicPlayerTrack *track = mplayInfo->tracks;
        for (i = 0; i < mplayInfo->trackCount; i++, track++)
        {
            struct SoundChannel *chan;

            if (!(track->flags & MPT_FLG_EXIST) || !(track->flags & (MPT_FLG_VOLCHG | MPT_FLG_PITCHG)))
                continue;
            TrkVolPitSet(mplayInfo, track);
            for (chan = track->chan; chan != NULL; chan = chan->nextChannelPointer)
            {
                u8 cgbType;

                if (!(chan->statusFlags & SOUND_CHANNEL_SF_ON))
                {
                    ClearChain(chan);
                    continue;
                }
                cgbType = chan->type & TONEDATA_TYPE_CGB;
                if (track->flags & MPT_FLG_VOLCHG)
                {
                    ChnVolSet(chan, track);
                    if (cgbType != 0)
                        ((struct CgbChannel *)chan)->modify |= CGB_CHANNEL_MO_VOL;
                }
                if (track->flags & MPT_FLG_PITCHG)
                {
                    s32 key = chan->key + track->keyM;
                    if (key < 0)
                        key = 0;
                    if (cgbType != 0)
                    {
                        chan->frequency = CallMidiKeyToCgbFreq(soundInfo, cgbType, key, track->pitM);
                        ((struct CgbChannel *)chan)->modify |= CGB_CHANNEL_MO_PIT;
                    }
                    else
                    {
                        chan->frequency = MidiKeyToFreq(chan->wav, key, track->pitM);
                    }
                }
            }
            track->flags &= 0xF0;
        }
    }

done:
    mplayInfo->ident = ID_NUMBER;
}

// --- the sound driver's main loop

// Called from the V-blank interrupt (m4aSoundMain): run every music player, the Game Boy
// channels' envelopes (CgbSound) and mix one frame of sound.
void SoundMain(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;

    if (soundInfo == NULL || soundInfo->ident != ID_NUMBER)
        return;
    soundInfo->ident++;

    if (soundInfo->MPlayMainHead != NULL)
        soundInfo->MPlayMainHead(soundInfo->musicPlayerHead);
    soundInfo->CgbSound();
    PlatformSound_MixFrame(soundInfo);

    soundInfo->ident = ID_NUMBER;
}

// Called from the V-count interrupt. On the GBA this restarts the sound DMA every
// pcmDmaPeriod frames; here only the counter matters (the Pokédex cry screen reads it).
void m4aSoundVSync(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;

    if (soundInfo == NULL || soundInfo->ident - ID_NUMBER > 1)
        return;
    if ((s32)soundInfo->pcmDmaCounter - 1 > 0)
        soundInfo->pcmDmaCounter--;
    else
        soundInfo->pcmDmaCounter = soundInfo->pcmDmaPeriod;
}

#endif // PORTABLE
