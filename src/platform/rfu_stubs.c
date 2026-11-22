// Pokémon World web/PC build: the GBA wireless adapter library, with no adapter.
// Every request fails, so the game behaves as if no wireless adapter is plugged in.

#ifdef PORTABLE

#include "global.h"
#include "librfu.h"

struct RfuLinkStatus *gRfuLinkStatus;
struct RfuSlotStatusNI *gRfuSlotStatusNI[RFU_CHILD_MAX];
struct RfuSlotStatusUNI *gRfuSlotStatusUNI[RFU_CHILD_MAX];
static u8 sNoRecvBuffer[0x100];

u16 rfu_initializeAPI(u32 *APIBuffer, u16 buffByteSize, IntrFunc *sioIntrTable_p, bool8 copyInterruptToRam)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_setTimerInterrupt(u8 timerNo, IntrFunc *timerIntrTable_p)
{
}

u16 rfu_syncVBlank(void)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_setREQCallback(void (*callback)(u16 reqCommandId, u16 reqResult))
{
}

u16 rfu_waitREQComplete(void)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u32 rfu_REQBN_softReset_and_checkID(void)
{
    return 0;
}

void rfu_REQ_reset(void)
{
}

void rfu_REQ_stopMode(void)
{
}

void rfu_REQ_configSystem(u16 availSlotFlag, u8 maxMFrame, u8 mcTimer)
{
}

void rfu_REQ_configGameData(u8 mbootFlag, u16 serialNo, const u8 *gname, const u8 *uname)
{
}

void rfu_REQ_startSearchChild(void)
{
}

void rfu_REQ_pollSearchChild(void)
{
}

void rfu_REQ_endSearchChild(void)
{
}

void rfu_REQ_startSearchParent(void)
{
}

void rfu_REQ_pollSearchParent(void)
{
}

void rfu_REQ_endSearchParent(void)
{
}

void rfu_REQ_startConnectParent(u16 pid)
{
}

void rfu_REQ_pollConnectParent(void)
{
}

void rfu_REQ_endConnectParent(void)
{
}

u16 rfu_getConnectParentStatus(u8 *status, u8 *connectSlotNo)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_REQ_CHILD_startConnectRecovery(u8 bmRecoverySlot)
{
}

void rfu_REQ_CHILD_pollConnectRecovery(void)
{
}

void rfu_REQ_CHILD_endConnectRecovery(void)
{
}

u16 rfu_CHILD_getConnectRecoveryStatus(u8 *status)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u16 rfu_REQBN_watchLink(u16 reqCommandId, u8 *bmLinkLossSlot, u8 *linkLossReason, u8 *parentBmLinkRecoverySlot)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_REQ_disconnect(u8 bmDisconnectSlot)
{
}

void rfu_REQ_changeMasterSlave(void)
{
}

bool8 rfu_getMasterSlave(void)
{
    return FALSE;
}

void rfu_setMSCCallback(void (*callback)(u16 reqCommandId))
{
}

void rfu_clearAllSlot(void)
{
}

u16 rfu_clearSlot(u8 connTypeFlag, u8 slotStatusIndex)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u16 rfu_setRecvBuffer(u8 connType, u8 slotNo, void *buffer, u32 buffSize)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u16 rfu_UNI_setSendData(u8 bmSendSlot, const void *src, u8 size)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_UNI_readySendData(u8 slotStatusIndex)
{
}

u16 rfu_UNI_PARENT_getDRAC_ACK(u8 *ackFlag)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_UNI_clearRecvNewDataFlag(u8 slotStatusIndex)
{
}

u16 rfu_NI_setSendData(u8 bmSendSlot, u8 subFrameSize, const void *src, u32 size)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u16 rfu_NI_CHILD_setSendGameName(u8 slotNo, u8 subFrameSize)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u16 rfu_NI_stopReceivingData(u8 slotStatusIndex)
{
    return 0x100; // ERR_REQ_CMD_ID
}

u16 rfu_changeSendTarget(u8 connType, u8 slotStatusIndex, u8 bmNewTgtSlot)
{
    return 0x100; // ERR_REQ_CMD_ID
}

void rfu_REQ_sendData(bool8 clockChangeFlag)
{
}

void rfu_REQ_PARENT_resumeRetransmitAndChange(void)
{
}

void rfu_REQ_recvData(void)
{
}

u8 * rfu_getSTWIRecvBuffer(void)
{
    return sNoRecvBuffer;
}

void rfu_REQ_RFUStatus(void)
{
}

u16 rfu_getRFUStatus(u8 *rfuState)
{
    return 0x100; // ERR_REQ_CMD_ID
}

#endif // PORTABLE
