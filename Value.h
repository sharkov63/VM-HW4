#pragma once

#include "fmt/format.h"
#include <assert.h>
#include <cstdint>

namespace lama {

using Value = int32_t;

inline bool valueIsInt(Value value) { return value & 1; }

inline bool valueIsPtr(Value value) { return !(value & 1); }

inline Value boxInt(int32_t num) { return (num << 1) | 1; }

inline int32_t unboxInt(Value value) {
  assert(valueIsInt(value));
  return value >> 1;
}

} // namespace lama
