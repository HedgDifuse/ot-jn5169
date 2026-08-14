/* otPlatAlarmMilli на базе TickTimer JN516x (16 МГц системный тик) */
#include <jendefs.h>
#include <AppHardwareApi.h>

#include <stdbool.h>
#include <stdint.h>

#include <openthread/platform/alarm-milli.h>
#include "platform-jn5169.h"

#define TICKS_PER_MS 16000u /* TickTimer тактируется 16 МГц */

static volatile uint32_t sMsCounter; /* монотонные миллисекунды со старта */
static uint32_t          sAlarmT0;
static uint32_t          sAlarmDt;
static bool              sAlarmActive;

static void tickTimerIsr(uint32 aDevice, uint32 aItemBitmap)
{
    (void)aDevice;
    (void)aItemBitmap;
    sMsCounter++;
}

void jn5169AlarmInit(void)
{
    sMsCounter   = 0;
    sAlarmActive = false;

    vAHI_TickTimerConfigure(E_AHI_TICK_TIMER_DISABLE);
    vAHI_TickTimerWrite(0);
    vAHI_TickTimerInterval(TICKS_PER_MS);
    vAHI_TickTimerInit(tickTimerIsr);
    vAHI_TickTimerIntEnable(TRUE);
    vAHI_TickTimerConfigure(E_AHI_TICK_TIMER_RESTART);
}

uint32_t otPlatAlarmMilliGetNow(void)
{
    return sMsCounter;
}

void otPlatAlarmMilliStartAt(otInstance *aInstance, uint32_t aT0, uint32_t aDt)
{
    (void)aInstance;
    sAlarmT0     = aT0;
    sAlarmDt     = aDt;
    sAlarmActive = true;
}

void otPlatAlarmMilliStop(otInstance *aInstance)
{
    (void)aInstance;
    sAlarmActive = false;
}

void jn5169AlarmProcess(otInstance *aInstance)
{
    if (sAlarmActive && (uint32_t)(sMsCounter - sAlarmT0) >= sAlarmDt)
    {
        sAlarmActive = false;
        otPlatAlarmMilliFired(aInstance);
    }
}
