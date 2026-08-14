/* otPlatRadio на MicroMAC (MMAC) JN5169, PHY-режим.
 *
 * ПЕРЕДАЧА — vMMAC_StartMacTransmit: аппаратный auto-ACK (матч по seq, надёжно).
 * Заголовок строит железо из tsMacFrame; aux-sec+payload+MIC — в uPayload. Раньше был
 * vMMAC_StartPhyTransmit: сырой PSDU (формат кадра строит ядро OT,
 * поэтому он заведомо корректен — на нём работали attach и tx-security). FCS
 * досчитываем программно (PHY-режим MMAC его не считает, подтверждено Contiki).
 * Приём ACK — аппаратный: флаг E_MMAC_TX_USE_AUTO_ACK валиден и для PHY-режима
 * (SDK: опция для StartMacTransmit ИЛИ StartPhyTransmit), MMAC матчит ACK по
 * номеру последовательности переданного кадра. Снимает гонку за 192 мкс.
 *
 * ПРИЁМ — vMMAC_StartMacReceive: АППАРАТНЫЙ auto-ACK и адресная фильтрация
 * (vMMAC_SetRxAddress). Это снимает программный ACK, который блокировал
 * приёмник на ~0.5мс и терял кадры в пачках (SRP/CASE-бурсты Matter).
 * PSDU реконструируем из tsMacFrame (эталон Contiki read()); aux-security-
 * заголовок Thread лежит в payload как есть (MMAC его не трогает). FCS
 * досчитываем. ПЕРЕДАЧА пока PHY+autoack (см. выше).
 */
#include <jendefs.h>
#include <AppHardwareApi.h>
#include <MMAC.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/radio.h>
#include <utils/mac_frame.h>
#include <utils/soft_source_match_table.h>
#include "platform-jn5169.h"

/* presence-флаги полей заголовка (jn_macframe.cpp) */
extern void jnMacPresence(const otRadioFrame *aFrame, uint8_t *aHasSeq, uint8_t *aHasDstPan, uint8_t *aHasSrcPan);

#define FCS_LEN 2
#define US_PER_SYMBOL 16u

#define LQI_TO_DBM(lqi) ((int8_t)(-95 + ((int16_t)(lqi) * 85) / 255))

enum
{
    kStateDisabled,
    kStateSleep,
    kStateReceive,
    kStateTransmit,
};

/* --- Состояние -------------------------------------------------------- */

static volatile uint8_t sState = kStateDisabled;

static tsMacFrame sRxMac;
static tsPhyFrame sTxPhy;
static tsMacFrame sTxMac;
static tsPhyFrame sAckPhy;

#define RX_SLOTS 2
typedef struct
{
    volatile bool valid;
    uint8_t       psdu[OT_RADIO_FRAME_MAX_SIZE];
    uint8_t       length;
    uint8_t       lqi;
    uint32_t      rxTimeSym;
} RxSlot;
static RxSlot sRxSlots[RX_SLOTS];

static otRadioFrame sTxFrame;
static otRadioFrame sAckFrame;
static uint8_t      sAckPsdu[16];

static volatile bool     sTxDone;
static volatile uint32_t sTxErrors;
static volatile bool     sAckTxInProgress;
static bool              sTxAckReq;

static uint8_t      sChannel = 11;
static int8_t       sTxPower = 8;
static uint16_t     sPanId = 0xFFFF;
static uint8_t      sEui64[OT_EXT_ADDRESS_SIZE];
static bool         sPromiscuous;
static bool         sScanRequested;
static uint8_t      sScanChannel;
static uint16_t     sScanDuration;

/* Контекст радио: адреса, ключи и счётчик кадров для tx-security.
 * mExtAddress — в little-endian (эфирный порядок), как в utils/mac_frame.h. */
static otRadioContext sCtx;

/* --- Программный FCS --------------------------------------------------- */

static uint16_t fcs16(const uint8_t *aData, uint8_t aLen)
{
    uint16_t crc = 0;
    uint8_t  i;

    while (aLen--)
    {
        crc ^= *aData++;
        for (i = 0; i < 8; i++)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
        }
    }

    return crc;
}

static void phyFinalize(tsPhyFrame *aPhy, uint16_t aLengthWithFcs)
{
    uint16_t crc = fcs16(aPhy->uPayload.au8Byte, (uint8_t)(aLengthWithFcs - FCS_LEN));

    aPhy->uPayload.au8Byte[aLengthWithFcs - 2] = (uint8_t)(crc & 0xFF);
    aPhy->uPayload.au8Byte[aLengthWithFcs - 1] = (uint8_t)(crc >> 8);
    aPhy->u8PayloadLength                      = (uint8_t)aLengthWithFcs;
}

/* --- Временнáя база --------------------------------------------------- */

static uint64_t sTimeHighSym;
static uint32_t sTimeLastSym;

static uint64_t radioTimeUs(void)
{
    uint32_t now = u32MMAC_GetTime();

    if (now < sTimeLastSym)
    {
        sTimeHighSym += 0x100000000ull;
    }
    sTimeLastSym = now;

    return (sTimeHighSym + now) * US_PER_SYMBOL;
}

static uint64_t symTimeToUs(uint32_t aSym)
{
    uint64_t high;

    radioTimeUs();
    high = (aSym > sTimeLastSym) ? (sTimeHighSym - 0x100000000ull) : sTimeHighSym;
    return (high + aSym) * US_PER_SYMBOL;
}

/* --- Внутреннее ------------------------------------------------------- */

/* Загрузить наш PAN/short/ext в аппаратный адресный фильтр MMAC.
 * on-air ext = реверс big-endian sCtx.mExtAddress; MMAC u32L/u32H —
 * инверсия реконструкции Contiki read(). */
static void updateRxAddress(void)
{
    const uint8_t *m = sCtx.mExtAddress.m8; /* big-endian */
    tsExtAddr      ext;

    /* канонический split (u32H=старшее слово), как vMMAC_GetMacAddress */
    ext.u32H = ((uint32)m[0] << 24) | ((uint32)m[1] << 16) | ((uint32)m[2] << 8) | m[3];
    ext.u32L = ((uint32)m[4] << 24) | ((uint32)m[5] << 16) | ((uint32)m[6] << 8) | m[7];

    vMMAC_SetRxAddress((uint32)sPanId, sCtx.mShortAddress, &ext);
}

static void startRx(void)
{
    vMMAC_StartMacReceive(&sRxMac,
                          (teRxOption)(E_MMAC_RX_START_NOW | E_MMAC_RX_USE_AUTO_ACK | E_MMAC_RX_NO_MALFORMED |
                                       E_MMAC_RX_NO_FCS_ERROR | E_MMAC_RX_ADDRESS_MATCH | E_MMAC_RX_ALIGN_NORMAL));
}

/* Реконструкция on-air PSDU из tsMacFrame (правила 2006; Thread data-кадры).
 * Возвращает длину с FCS, 0 при переполнении. */
static uint8_t reconstructPsdu(const tsMacFrame *m, uint8_t *out)
{
    uint16_t fcf     = m->u16FCF;
    uint8_t  dstMode = (uint8_t)((fcf >> 10) & 3);
    uint8_t  srcMode = (uint8_t)((fcf >> 14) & 3);
    uint8_t  panComp = (uint8_t)((fcf >> 6) & 1);
    uint16_t len     = 0;
    uint16_t crc;

    out[len++] = (uint8_t)(fcf & 0xFF);
    out[len++] = (uint8_t)(fcf >> 8);
    out[len++] = m->u8SequenceNum;

    if (dstMode != 0)
    {
        out[len++] = (uint8_t)(m->u16DestPAN & 0xFF);
        out[len++] = (uint8_t)(m->u16DestPAN >> 8);
    }
    if (dstMode == 2)
    {
        out[len++] = (uint8_t)(m->uDestAddr.u16Short & 0xFF);
        out[len++] = (uint8_t)(m->uDestAddr.u16Short >> 8);
    }
    else if (dstMode == 3)
    {
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32L);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32L >> 8);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32L >> 16);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32L >> 24);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32H);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32H >> 8);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32H >> 16);
        out[len++] = (uint8_t)(m->uDestAddr.sExt.u32H >> 24);
    }

    if (srcMode != 0 && !panComp)
    {
        out[len++] = (uint8_t)(m->u16SrcPAN & 0xFF);
        out[len++] = (uint8_t)(m->u16SrcPAN >> 8);
    }
    if (srcMode == 2)
    {
        out[len++] = (uint8_t)(m->uSrcAddr.u16Short & 0xFF);
        out[len++] = (uint8_t)(m->uSrcAddr.u16Short >> 8);
    }
    else if (srcMode == 3)
    {
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32L);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32L >> 8);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32L >> 16);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32L >> 24);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32H);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32H >> 8);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32H >> 16);
        out[len++] = (uint8_t)(m->uSrcAddr.sExt.u32H >> 24);
    }

    /* Точная проверка: заголовок(len) + payload + FCS должны влезть в PSDU.
     * Старая +25 (худший случай ext+ext) ложно отвергала крупные кадры с
     * короткой адресацией (первые фрагменты 6LoWPAN, MLE Link Accept). */
    if ((uint16_t)len + m->u8PayloadLength + FCS_LEN > OT_RADIO_FRAME_MAX_SIZE)
    {
        return 0;
    }
    memcpy(&out[len], m->uPayload.au8Byte, m->u8PayloadLength);
    len += m->u8PayloadLength;

    crc        = fcs16(out, (uint8_t)len);
    out[len++] = (uint8_t)(crc & 0xFF);
    out[len++] = (uint8_t)(crc >> 8);

    return (uint8_t)len;
}

static bool frameForUs(const otRadioFrame *aFrame)
{
    if (sPromiscuous)
    {
        return true;
    }

    return otMacFrameDoesAddrMatchAny(aFrame, sPanId, sCtx.mShortAddress, sCtx.mAlternateShortAddress,
                                     &sCtx.mExtAddress);
}

static void mmacIsr(uint32 aEvents)
{
    if (aEvents & E_MMAC_INT_RX_COMPLETE)
    {
        /* Кадр уже прошёл аппаратный адресный фильтр и auto-ACK. */
        if (u32MMAC_GetRxErrors() == 0 && sRxMac.u8PayloadLength > 0)
        {
            uint8_t frameType = (uint8_t)(sRxMac.u16FCF & 0x07);
            uint8_t slot;

            /* ACK-кадры (тип 2) нам не нужны — их обрабатывает TX-autoack */
            if (frameType != 2)
            {
                for (slot = 0; slot < RX_SLOTS; slot++)
                {
                    if (!sRxSlots[slot].valid)
                    {
                        uint8_t len = reconstructPsdu(&sRxMac, sRxSlots[slot].psdu);
                        uint8_t msq;

                        if (len >= 5)
                        {
                            sRxSlots[slot].length    = len;
                            sRxSlots[slot].lqi       = u8MMAC_GetRxLqi(&msq);
                            sRxSlots[slot].rxTimeSym = u32MMAC_GetRxTime();
                            sRxSlots[slot].valid     = true;
                        }
                        break;
                    }
                }
            }
        }

        startRx();
    }

    if (aEvents & E_MMAC_INT_TX_COMPLETE)
    {
        if (sAckTxInProgress)
        {
            sAckTxInProgress = false;
        }
        else
        {
            /* GetTxErrors уже учёл аппаратное ожидание ACK (USE_AUTO_ACK) */
            sTxErrors = u32MMAC_GetTxErrors();
            sTxDone   = true;
        }

        startRx();
        sState = kStateReceive;
    }
}

/* --- tx-security ------------------------------------------------------ */

static void processTxSecurity(otRadioFrame *aFrame)
{
    uint8_t           keyId;
    otMacKeyMaterial *key;

    if (!otMacFrameIsSecurityEnabled(aFrame))
    {
        return;
    }
    if (!otMacFrameIsKeyIdMode1(aFrame))
    {
        return;
    }

    /* RCP владеет tx-security (cap TRANSMIT_SEC): НЕ доверяем mIsSecurityProcessed
     * от хоста. При retransmit хост шлёт кадр с ПЛЕЙНТЕКСТОМ, но помечает его
     * обработанным — если верить, кадр уйдёт НЕзашифрованным (MIC=0). */
    if (!aFrame->mInfo.mTxInfo.mIsHeaderUpdated)
    {
        otMacFrameSetKeyId(aFrame, sCtx.mKeyId);
        otMacFrameSetFrameCounter(aFrame, sCtx.mMacFrameCounter++);
        aFrame->mInfo.mTxInfo.mIsHeaderUpdated = true;
    }

    keyId = otMacFrameGetKeyId(aFrame);
    if (keyId == sCtx.mKeyId)
    {
        key = &sCtx.mCurrKey;
    }
    else if (keyId == (uint8_t)(sCtx.mKeyId - 1))
    {
        key = &sCtx.mPrevKey;
    }
    else if (keyId == (uint8_t)(sCtx.mKeyId + 1))
    {
        key = &sCtx.mNextKey;
    }
    else
    {
        key = &sCtx.mCurrKey;
    }

    aFrame->mInfo.mTxInfo.mAesKey              = key;
    aFrame->mInfo.mTxInfo.mIsSecurityProcessed = false; /* заставить AES выполниться */
    otMacFrameProcessTransmitAesCcm(aFrame, &sCtx.mExtAddress);
}

/* --- Инициализация / процесс ------------------------------------------ */

void jn5169RadioInit(void)
{
    tsExtAddr mac;

    sTxFrame.mPsdu  = sTxPhy.uPayload.au8Byte;
    sAckFrame.mPsdu = sAckPsdu;

    memset(&sCtx, 0, sizeof(sCtx));
    sCtx.mShortAddress          = 0xFFFE;
    sCtx.mAlternateShortAddress = 0xFFFE; /* kShortAddrInvalid: не участвует в match */

    vMMAC_Enable();
    vMMAC_EnableInterrupts(mmacIsr);
    vMMAC_ConfigureInterruptSources(E_MMAC_INT_TX_COMPLETE | E_MMAC_INT_RX_COMPLETE);
    vMMAC_ConfigureRadio();
    vMMAC_SetChannelAndPower(sChannel, sTxPower);
    vMMAC_SetCcaMode(E_MMAC_CCAMODE_ENERGY);
    vMMAC_SetTxParameters(4, 3, 5, 4);
    updateRxAddress();

    vMMAC_GetMacAddress(&mac);
    sEui64[0] = (uint8_t)(mac.u32H >> 24);
    sEui64[1] = (uint8_t)(mac.u32H >> 16);
    sEui64[2] = (uint8_t)(mac.u32H >> 8);
    sEui64[3] = (uint8_t)(mac.u32H);
    sEui64[4] = (uint8_t)(mac.u32L >> 24);
    sEui64[5] = (uint8_t)(mac.u32L >> 16);
    sEui64[6] = (uint8_t)(mac.u32L >> 8);
    sEui64[7] = (uint8_t)(mac.u32L);
}

void jn5169RadioProcess(otInstance *aInstance)
{
    uint8_t slot;

    if (sScanRequested)
    {
        uint32_t symbols = (uint32_t)sScanDuration * 1000u / US_PER_SYMBOL;
        uint8_t  ed;

        sScanRequested = false;
        vMMAC_SetChannel(sScanChannel);
        ed = u8MMAC_EnergyDetect(symbols);
        vMMAC_SetChannel(sChannel);
        startRx();
        otPlatRadioEnergyScanDone(aInstance, LQI_TO_DBM(ed));
    }

    for (slot = 0; slot < RX_SLOTS; slot++)
    {
        if (sRxSlots[slot].valid)
        {
            otRadioFrame frame;

            memset(&frame, 0, sizeof(frame));
            frame.mPsdu               = sRxSlots[slot].psdu;
            frame.mLength             = sRxSlots[slot].length;
            frame.mChannel            = sChannel;
            frame.mInfo.mRxInfo.mLqi  = sRxSlots[slot].lqi;
            frame.mInfo.mRxInfo.mRssi = LQI_TO_DBM(sRxSlots[slot].lqi);
            frame.mInfo.mRxInfo.mTimestamp = symTimeToUs(sRxSlots[slot].rxTimeSym);
            frame.mInfo.mRxInfo.mAckedWithFramePending = true;

            otPlatRadioReceiveDone(aInstance, &frame, OT_ERROR_NONE);
            sRxSlots[slot].valid = false;
        }
    }

    if (sTxDone)
    {
        uint32_t errors = sTxErrors;
        otError  error;

        sTxDone = false;

        error = (errors == 0)                     ? OT_ERROR_NONE
              : (errors & E_MMAC_TXSTAT_CCA_BUSY) ? OT_ERROR_CHANNEL_ACCESS_FAILURE
              : (errors & E_MMAC_TXSTAT_NO_ACK)   ? OT_ERROR_NO_ACK
                                                  : OT_ERROR_ABORT;

        if (error == OT_ERROR_NONE && sTxAckReq)
        {
            otMacFrameGenerateImmAck(&sTxFrame, false, &sAckFrame);
            sAckFrame.mInfo.mRxInfo.mTimestamp = radioTimeUs();
            otPlatRadioTxDone(aInstance, &sTxFrame, &sAckFrame, error);
        }
        else
        {
            otPlatRadioTxDone(aInstance, &sTxFrame, NULL, error);
        }
    }
}

/* --- Управление состоянием -------------------------------------------- */

otError otPlatRadioEnable(otInstance *aInstance)
{
    (void)aInstance;
    if (sState == kStateDisabled)
    {
        sState = kStateSleep;
    }
    return OT_ERROR_NONE;
}

otError otPlatRadioDisable(otInstance *aInstance)
{
    (void)aInstance;
    vMMAC_RadioOff();
    sState = kStateDisabled;
    return OT_ERROR_NONE;
}

bool otPlatRadioIsEnabled(otInstance *aInstance)
{
    (void)aInstance;
    return sState != kStateDisabled;
}

otError otPlatRadioSleep(otInstance *aInstance)
{
    (void)aInstance;
    vMMAC_RadioOff();
    sState = kStateSleep;
    return OT_ERROR_NONE;
}

otError otPlatRadioReceive(otInstance *aInstance, uint8_t aChannel)
{
    (void)aInstance;

    if (sState == kStateDisabled)
    {
        return OT_ERROR_INVALID_STATE;
    }

    if (aChannel != sChannel)
    {
        sChannel = aChannel;
        vMMAC_SetChannelAndPower(sChannel, sTxPower);
    }

    startRx();
    sState = kStateReceive;
    return OT_ERROR_NONE;
}

/* --- Передача (PHY + аппаратный auto-ack) ----------------------------- */

otRadioFrame *otPlatRadioGetTransmitBuffer(otInstance *aInstance)
{
    (void)aInstance;
    return &sTxFrame;
}

otError otPlatRadioTransmit(otInstance *aInstance, otRadioFrame *aFrame)
{
    const uint8_t *psdu = aFrame->mPsdu;
    uint16_t       fcf;
    uint8_t        dstMode, srcMode, hasSeq, hasDstPan, hasSrcPan;
    uint16_t       idx = 2;
    uint16_t       payloadLen;
    teTxOption     opt;

    if (sState == kStateDisabled || sState == kStateTransmit)
    {
        return OT_ERROR_INVALID_STATE;
    }

    if (aFrame->mChannel != sChannel)
    {
        sChannel = aFrame->mChannel;
        vMMAC_SetChannelAndPower(sChannel, sTxPower);
    }

    processTxSecurity(aFrame); /* шифрует mPsdu на месте */

    if (aFrame->mInfo.mTxInfo.mTxDelay != 0)
    {
        uint32_t targetUs = aFrame->mInfo.mTxInfo.mTxDelayBaseTime + aFrame->mInfo.mTxInfo.mTxDelay;
        vMMAC_SetTxStartTime(targetUs / US_PER_SYMBOL);
        opt = E_MMAC_TX_DELAY_START;
    }
    else
    {
        opt = E_MMAC_TX_START_NOW;
    }

    /* Разбор PSDU → tsMacFrame (адресная часть); aux-sec+payload+MIC — в uPayload.
     * ext: канонический split (u32H=старшее слово), on-air байты LE. */
    fcf     = (uint16_t)(psdu[0] | (psdu[1] << 8));
    dstMode = (uint8_t)((fcf >> 10) & 3);
    srcMode = (uint8_t)((fcf >> 14) & 3);
    jnMacPresence(aFrame, &hasSeq, &hasDstPan, &hasSrcPan);

    sTxMac.u16FCF    = fcf;
    sTxMac.u16FCS    = 0;
    sTxMac.u16Unused = 0;
    sTxMac.u16DestPAN = 0xFFFF;
    sTxMac.u16SrcPAN  = 0xFFFF;
    sTxMac.u8SequenceNum = hasSeq ? psdu[idx] : 0;
    if (hasSeq)
    {
        idx += 1;
    }
    if (hasDstPan)
    {
        sTxMac.u16DestPAN = (uint16_t)(psdu[idx] | (psdu[idx + 1] << 8));
        idx += 2;
    }
    if (dstMode == 2)
    {
        sTxMac.uDestAddr.u16Short = (uint16_t)(psdu[idx] | (psdu[idx + 1] << 8));
        idx += 2;
    }
    else if (dstMode == 3)
    {
        sTxMac.uDestAddr.sExt.u32H =
            ((uint32)psdu[idx + 7] << 24) | ((uint32)psdu[idx + 6] << 16) | ((uint32)psdu[idx + 5] << 8) | psdu[idx + 4];
        sTxMac.uDestAddr.sExt.u32L =
            ((uint32)psdu[idx + 3] << 24) | ((uint32)psdu[idx + 2] << 16) | ((uint32)psdu[idx + 1] << 8) | psdu[idx];
        idx += 8;
    }
    if (hasSrcPan)
    {
        sTxMac.u16SrcPAN = (uint16_t)(psdu[idx] | (psdu[idx + 1] << 8));
        idx += 2;
    }
    if (srcMode == 2)
    {
        sTxMac.uSrcAddr.u16Short = (uint16_t)(psdu[idx] | (psdu[idx + 1] << 8));
        idx += 2;
    }
    else if (srcMode == 3)
    {
        sTxMac.uSrcAddr.sExt.u32H =
            ((uint32)psdu[idx + 7] << 24) | ((uint32)psdu[idx + 6] << 16) | ((uint32)psdu[idx + 5] << 8) | psdu[idx + 4];
        sTxMac.uSrcAddr.sExt.u32L =
            ((uint32)psdu[idx + 3] << 24) | ((uint32)psdu[idx + 2] << 16) | ((uint32)psdu[idx + 1] << 8) | psdu[idx];
        idx += 8;
    }

    if ((uint16_t)(idx + FCS_LEN) > aFrame->mLength)
    {
        return OT_ERROR_ABORT;
    }
    payloadLen = (uint16_t)(aFrame->mLength - FCS_LEN - idx);
    memcpy(sTxMac.uPayload.au8Byte, &psdu[idx], payloadLen);
    sTxMac.u8PayloadLength = (uint8_t)payloadLen;

    if (aFrame->mInfo.mTxInfo.mCsmaCaEnabled)
    {
        opt = (teTxOption)(opt | E_MMAC_TX_USE_CCA);
    }
    sTxAckReq = otMacFrameIsAckRequested(aFrame);
    if (sTxAckReq)
    {
        opt = (teTxOption)(opt | E_MMAC_TX_USE_AUTO_ACK);
    }

    sState = kStateTransmit;
    vMMAC_StartMacTransmit(&sTxMac, opt);
    otPlatRadioTxStarted(aInstance, aFrame);
    return OT_ERROR_NONE;
}

/* --- Параметры -------------------------------------------------------- */

void otPlatRadioGetIeeeEui64(otInstance *aInstance, uint8_t *aIeeeEui64)
{
    (void)aInstance;
    memcpy(aIeeeEui64, sEui64, OT_EXT_ADDRESS_SIZE);
}

void otPlatRadioSetPanId(otInstance *aInstance, otPanId aPanId)
{
    (void)aInstance;
    sPanId = aPanId;
    updateRxAddress();
}

void otPlatRadioSetExtendedAddress(otInstance *aInstance, const otExtAddress *aExtAddress)
{
    uint8_t i;

    (void)aInstance;

    /* приходит big-endian → в контекст little-endian */
    for (i = 0; i < OT_EXT_ADDRESS_SIZE; i++)
    {
        sCtx.mExtAddress.m8[i] = aExtAddress->m8[OT_EXT_ADDRESS_SIZE - 1 - i];
    }
    updateRxAddress();
}

void otPlatRadioSetShortAddress(otInstance *aInstance, otShortAddress aShortAddress)
{
    (void)aInstance;
    sCtx.mShortAddress = aShortAddress;
    updateRxAddress();
}

otError otPlatRadioGetTransmitPower(otInstance *aInstance, int8_t *aPower)
{
    (void)aInstance;
    *aPower = sTxPower;
    return OT_ERROR_NONE;
}

otError otPlatRadioSetTransmitPower(otInstance *aInstance, int8_t aPower)
{
    (void)aInstance;
    sTxPower = aPower;
    vMMAC_SetChannelAndPower(sChannel, sTxPower);
    return OT_ERROR_NONE;
}

int8_t otPlatRadioGetRssi(otInstance *aInstance)
{
    (void)aInstance;
    return LQI_TO_DBM(u8MMAC_EnergyDetect(8));
}

otRadioCaps otPlatRadioGetCaps(otInstance *aInstance)
{
    (void)aInstance;
    return (otRadioCaps)(OT_RADIO_CAPS_CSMA_BACKOFF | OT_RADIO_CAPS_TRANSMIT_RETRIES | OT_RADIO_CAPS_ACK_TIMEOUT |
                         OT_RADIO_CAPS_ENERGY_SCAN | OT_RADIO_CAPS_TRANSMIT_SEC | OT_RADIO_CAPS_TRANSMIT_TIMING);
}

uint64_t otPlatRadioGetNow(otInstance *aInstance)
{
    (void)aInstance;
    return radioTimeUs();
}

bool otPlatRadioGetPromiscuous(otInstance *aInstance)
{
    (void)aInstance;
    return sPromiscuous;
}

void otPlatRadioSetPromiscuous(otInstance *aInstance, bool aEnable)
{
    (void)aInstance;
    sPromiscuous = aEnable;
}

otError otPlatRadioEnergyScan(otInstance *aInstance, uint8_t aScanChannel, uint16_t aScanDuration)
{
    (void)aInstance;
    sScanChannel   = aScanChannel;
    sScanDuration  = aScanDuration;
    sScanRequested = true;
    return OT_ERROR_NONE;
}

int8_t otPlatRadioGetReceiveSensitivity(otInstance *aInstance)
{
    (void)aInstance;
    return -95;
}

/* --- MAC-ключи (tx-security) ------------------------------------------ */

void otPlatRadioSetMacKey(otInstance             *aInstance,
                          uint8_t                 aKeyIdMode,
                          uint8_t                 aKeyIndex,
                          const otMacKeyMaterial *aPrevKey,
                          const otMacKeyMaterial *aCurrKey,
                          const otMacKeyMaterial *aNextKey,
                          otRadioKeyType          aKeyType)
{
    (void)aInstance;
    (void)aKeyIdMode;

    sCtx.mKeyId   = aKeyIndex;
    sCtx.mKeyType = aKeyType;
    /* НЕ сбрасывать счётчик в 0: хост может вызвать SetMacKey в середине сессии,
     * и наши кадры ушли бы с низкими счётчиками, которые пир бракует как replay.
     * Счётчик задаёт SetMacFrameCounter; тут только запоминаем предыдущий. */
    sCtx.mPrevMacFrameCounter = sCtx.mMacFrameCounter;
    sCtx.mPrevKey             = *aPrevKey;
    sCtx.mCurrKey             = *aCurrKey;
    sCtx.mNextKey             = *aNextKey;
}

void otPlatRadioSetMacFrameCounter(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    (void)aInstance;
    sCtx.mMacFrameCounter = aMacFrameCounter;
}

void otPlatRadioSetMacFrameCounterIfLarger(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    (void)aInstance;
    if (aMacFrameCounter > sCtx.mMacFrameCounter)
    {
        sCtx.mMacFrameCounter = aMacFrameCounter;
    }
}

/* --- Source match ----------------------------------------------------- */

void otPlatRadioEnableSrcMatch(otInstance *aInstance, bool aEnable)
{
    (void)aInstance;
    (void)aEnable;
}

otError otPlatRadioGetCcaEnergyDetectThreshold(otInstance *aInstance, int8_t *aThreshold)
{
    (void)aInstance;
    (void)aThreshold;
    return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatRadioSetCcaEnergyDetectThreshold(otInstance *aInstance, int8_t aThreshold)
{
    (void)aInstance;
    (void)aThreshold;
    return OT_ERROR_NOT_IMPLEMENTED;
}
