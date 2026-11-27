#pragma once

#if defined(GIGA_EDGE_EXPERIMENTAL_TFLITE_C_API)
#include "tensorflow/lite/experimental/c/c_api.h"
#else
#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/c/common.h"
#endif
