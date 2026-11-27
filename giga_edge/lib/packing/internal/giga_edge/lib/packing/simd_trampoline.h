#pragma once

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define GIGA_EDGE_PACKING_HAS_ARM_NEON 1
#include <arm_neon.h>
#else
#define GIGA_EDGE_PACKING_HAS_ARM_NEON 0
#endif

#if defined(__has_attribute) && __has_attribute(vector_size)
#define GIGA_EDGE_PACKING_HAS_VECTOR_EXTENSION 1
#else
#define GIGA_EDGE_PACKING_HAS_VECTOR_EXTENSION 0
#endif
