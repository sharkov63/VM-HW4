#pragma once

#include "fmt/format.h"
#include <stdexcept>

namespace lama {

template <typename... A>
[[noreturn]] void runtimeError(A &&...args) {
  throw std::runtime_error(fmt::format(std::forward<decltype(args)>(args)...));
}

} // namespace lama
