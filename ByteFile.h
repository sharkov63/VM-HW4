#pragma once

#include <memory>

namespace lama {

class GlobalArea;

class ByteFile {
public:
  ByteFile() = default;
  ByteFile(std::unique_ptr<const uint8_t[]> data, size_t sizeBytes);

  static ByteFile load(std::string path);

  const uint8_t *getCode() const { return code; }
  size_t getCodeSizeBytes() { return codeSizeBytes; }

  const char *getStringTable() const { return stringTable; }
  size_t getStringTableSize() const { return stringTableSizeBytes; }

  size_t getPublicSymbolNum() const { return publicSymbolsNum; }
  const int32_t *getPublicSymbolTable() const { return publicSymbolTable; }

  [[deprecated]]
  const uint8_t *getAddressFor(size_t offset) const;

  [[deprecated]]
  const char *getStringAt(size_t offset) const;

  size_t getGlobalAreaSize() const { return globalAreaSizeWords; }

private:
  void init();

private:
  std::unique_ptr<const uint8_t[]> data;
  size_t sizeBytes;

  const char *stringTable;
  size_t stringTableSizeBytes;

  const int32_t *publicSymbolTable;
  size_t publicSymbolsNum;

  const uint8_t *code;
  size_t codeSizeBytes;

  size_t globalAreaSizeWords;
};

} // namespace lama
