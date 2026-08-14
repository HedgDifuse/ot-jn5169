/* Внутренний заголовок платформы JN5169 */
#ifndef PLATFORM_JN5169_H_
#define PLATFORM_JN5169_H_

#include <stdint.h>
#include <openthread/instance.h>

/* Заголовки NXP SDK (JN-SW-4163). Имена сверить после распаковки SDK. */
/* #include <jendefs.h>      */
/* #include <AppHardwareApi.h> — AHI: UART, TickTimer, RNG, sw reset */
/* #include <MMAC.h>           — MicroMAC: 802.15.4 радио            */

void jn5169AlarmInit(void);
void jn5169AlarmProcess(otInstance *aInstance);

void jn5169RadioInit(void);
void jn5169RadioProcess(otInstance *aInstance);

void jn5169UartInit(void);
void jn5169UartProcess(void);

#endif /* PLATFORM_JN5169_H_ */
