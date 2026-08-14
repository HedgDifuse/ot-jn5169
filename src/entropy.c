/* otPlatEntropy на аппаратном ГСЧ JN516x */
#include <jendefs.h>
#include <AppHardwareApi.h>

#include <stddef.h>
#include <stdint.h>

#include <openthread/error.h>
#include <openthread/platform/entropy.h>

otError otPlatEntropyGet(uint8_t *aOutput, uint16_t aOutputLength)
{
    uint16_t i;

    if (aOutput == NULL)
    {
        return OT_ERROR_INVALID_ARGS;
    }

    for (i = 0; i < aOutputLength; i += 2)
    {
        uint16_t r;

        vAHI_StartRandomNumberGenerator(E_AHI_RND_SINGLE_SHOT, E_AHI_INTS_DISABLED);
        while (!bAHI_RndNumPoll())
        {
        }
        r = u16AHI_ReadRandomNumber();

        aOutput[i] = (uint8_t)(r & 0xFF);
        if ((uint16_t)(i + 1) < aOutputLength)
        {
            aOutput[i + 1] = (uint8_t)(r >> 8);
        }
    }

    return OT_ERROR_NONE;
}
