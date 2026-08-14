/* otSysInit / главный цикл драйверов для JN5169 */
#include <jendefs.h>
#include <AppHardwareApi.h>
#include <PeripheralRegs.h>

#include <openthread-system.h>
#include "platform-jn5169.h"

void otSysInit(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    u32AHI_Init();
    vAHI_WatchdogStop();

    jn5169AlarmInit();
    jn5169UartInit();
    jn5169RadioInit();
}

bool otSysPseudoResetWasRequested(void)
{
    return false;
}

void otSysDeinit(void)
{
}

void otSysProcessDrivers(otInstance *aInstance)
{
    jn5169UartProcess();
    jn5169RadioProcess(aInstance);
    jn5169AlarmProcess(aInstance);
}
