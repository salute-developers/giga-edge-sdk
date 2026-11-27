#pragma once

#include "giga_edge/modules/model/backends/tflite/trampoline.h"

namespace giga_edge {

void RegisterUnpack5Bit(TfLiteInterpreterOptions* options);

} // namespace giga_edge
