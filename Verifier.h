#pragma once

#include <stdexcept>

namespace lama {

class ByteFile;

class InvalidByteFileError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/// \throws InvalidByteFileError
void verify(ByteFile &file);

} // namespace lama
