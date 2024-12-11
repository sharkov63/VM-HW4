#pragma once

#include <memory>

namespace lama {

class GlobalArea;

class ByteFile {
public:
  ByteFile() = default;
  ByteFile(std::unique_ptr<const char[]> data, size_t sizeBytes);

  static ByteFile load(std::string path);

  const char *getCode() const { return code; }
  size_t getCodeSizeBytes() { return codeSizeBytes; }

  size_t getPublicSymbolNum() const { return publicSymbolsNum; }
  const int32_t *getPublicSymbolTable() const { return publicSymbolTable; }

  const char *getAddressFor(size_t offset) const;

  const char *getStringAt(size_t offset) const;

private:
  void init();

private:
  std::unique_ptr<const char[]> data;
  size_t sizeBytes;

  const char *stringTable;
  size_t stringTableSizeBytes;

  const int32_t *publicSymbolTable;
  size_t publicSymbolsNum;

  const char *code;
  size_t codeSizeBytes;
};

} // namespace lama
