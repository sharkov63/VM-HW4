#include "ByteFile.h"
#include "Error.h"
#include <fstream>

using namespace lama;

ByteFile::ByteFile(std::unique_ptr<const uint8_t[]> data, size_t sizeBytes)
    : data(std::move(data)), sizeBytes(sizeBytes) {
  init();
}

static void throwOnInvalidFile(std::string message) {
  runtimeError("invalid bytefile: {}", message);
}

void ByteFile::init() {
  if (sizeBytes < 3 * sizeof(int32_t))
    throwOnInvalidFile("bytefile to small to contain header");
  const int *header = reinterpret_cast<const int *>(data.get());

  size_t currentOffset = 0;

  stringTableSizeBytes = header[0];
  if (stringTableSizeBytes < 0) {
    throwOnInvalidFile(fmt::format("string table size is negative ({})",
                                   stringTableSizeBytes));
  }
  currentOffset += sizeof(int32_t);

  globalAreaSizeWords = header[1];
  if (globalAreaSizeWords < 0) {
    throwOnInvalidFile(fmt::format(
        fmt::format("global area size is negative ({})", globalAreaSizeWords)));
  }
  currentOffset += sizeof(int32_t);

  publicSymbolsNum = header[2];
  if (publicSymbolsNum < 0) {
    throwOnInvalidFile(fmt::format("number of public symbols is negative ({})",
                                   publicSymbolsNum));
  }
  currentOffset += sizeof(int32_t);

  size_t publicSymbolTableSizeBytes = publicSymbolsNum * 2 * sizeof(int32_t);
  publicSymbolTable = reinterpret_cast<const int *>(data.get() + currentOffset);
  if (currentOffset + publicSymbolTableSizeBytes > sizeBytes) {
    throwOnInvalidFile(fmt::format(
        "bytefile is too small to hold public symbol table of {} bytes",
        publicSymbolTableSizeBytes));
  }
  currentOffset += publicSymbolTableSizeBytes;

  if (currentOffset + stringTableSizeBytes > sizeBytes) {
    throwOnInvalidFile(
        fmt::format("bytefile is too small to hold string table of {} bytes",
                    stringTableSizeBytes));
  }
  stringTable = reinterpret_cast<const char *>(data.get() + currentOffset);
  currentOffset += stringTableSizeBytes;

  code = data.get() + currentOffset;
  codeSizeBytes = sizeBytes - currentOffset;
}

ByteFile ByteFile::load(std::string path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (stream.fail()) {
    runtimeError("failed to read bytecode from {}", path);
  }
  auto sizeBytes = stream.tellg();
  stream.seekg(0, std::ios::beg);
  std::unique_ptr<uint8_t[]> data(new uint8_t[sizeBytes]);
  stream.read((char *)data.get(), sizeBytes);
  return ByteFile(std::move(data), sizeBytes);
}
