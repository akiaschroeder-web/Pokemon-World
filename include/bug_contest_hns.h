#ifndef GUARD_BUG_CONTEST_HNS_H
#define GUARD_BUG_CONTEST_HNS_H

// Pokémon World: National Park's Bug-Catching Contest, ported from Heart & Soul
// (src/hns_bug_contest.c; credited in PROJECT_CREDITS.md).

#define HNS_BUG_CONTEST_TIME_LIMIT_FRAMES (60 * 60 * 8) // 8 minutes at 60 FPS, as in Heart & Soul

bool32 Hns_GetBugContestFlag(void);
bool32 Hns_BugContestCheckTimeLimit(void);
void Hns_BugContestRetirePrompt(void);
void Hns_CB2_EndBugContestBattle(void);
void CB2_HnsBugContestWhiteOut(void); // overworld.c

// specials
void Hns_EnterBugContestMode(void);
void Hns_ExitBugContestMode(void);
void Hns_JudgeBugContestMon(void);
void Hns_TransferBugContestMon(void);
void Hns_RemoveSafariBalls(void);
void Hns_ShowBugContestChosenMon(void);

extern const u8 BugContest_EventScript_TimesUp_Hns[];
extern const u8 BugContest_EventScript_WhiteOut_Hns[];

#endif // GUARD_BUG_CONTEST_HNS_H
