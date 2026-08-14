/* otPlatReset и прочая мелочь */
#include <jendefs.h>
#include <AppHardwareApi.h>

#include <openthread/platform/misc.h>

void otPlatReset(otInstance *aInstance)
{
    (void)aInstance;
    vAHI_SwReset();

    for (;;)
    {
    }
}

otPlatResetReason otPlatGetResetReason(otInstance *aInstance)
{
    (void)aInstance;
    return OT_PLAT_RESET_REASON_POWER_ON;
}

void otPlatWakeHost(void)
{
    /* Хост (i.MX6) не спит — не требуется */
}
