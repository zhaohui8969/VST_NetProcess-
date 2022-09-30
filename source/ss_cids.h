//------------------------------------------------------------------------
// Copyright(c) 2022 natas.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace MyCompanyName {
//------------------------------------------------------------------------
static const Steinberg::FUID kNetProcessProcessorUID (0xD12E2F4D, 0xB12258BD, 0xA3EAF720, 0xEDB3C40C);
static const Steinberg::FUID kNetProcessControllerUID (0xFBAC239C, 0x51FE528E, 0x80169B16, 0x10D7F66D);

#define NetProcessVST3Category "Fx"

//------------------------------------------------------------------------
} // namespace MyCompanyName
