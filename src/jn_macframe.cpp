/* Presence-флаги полей заголовка через ot::Mac::Frame (2006/2015). */
#include "mac/mac_frame.hpp"

using namespace ot;

extern "C" void jnMacPresence(const otRadioFrame *aFrame,
                              uint8_t            *aHasSeq,
                              uint8_t            *aHasDstPan,
                              uint8_t            *aHasSrcPan)
{
    const Mac::Frame &frame = *static_cast<const Mac::Frame *>(aFrame);
    *aHasSeq    = frame.IsSequencePresent() ? 1 : 0;
    *aHasDstPan = frame.IsDstPanIdPresent() ? 1 : 0;
    *aHasSrcPan = frame.IsSrcPanIdPresent() ? 1 : 0;
}
