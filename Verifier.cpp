#include "Verifier.h"
#include "ByteFile.h"
#include "Error.h"
#include "Parser.h"
#include <assert.h>
#include <cstdint>
#include <memory>
#include <vector>

using namespace lama;

template <typename... A>
[[noreturn]] static void invalidByteFileError(A &&...args) {
  throw InvalidByteFileError(
      fmt::format(std::forward<decltype(args)>(args)...));
}

namespace {

union InstInfo {
  int32_t basicBlockNum;
  struct {
    char rift = false;
    char visited = false;
    int16_t operandStackSize;
  };
};

class Verifier {
public:
  Verifier(ByteFile &file);

  /// \throws InvalidByteFileError on invalid bytefile
  void verify();

private:
  void verifyStringTable();
  /// \throws InvalidByteFileError on invalid public symbol table
  void verifyPublicSymTab();
  void analyzeControlFlow();

  void analyzeAt(const uint8_t *ip);

  const uint8_t *lookUpIp(int32_t ioffset);

  /// \pre #ip is valid
  /// \pre currentOperandStackSize >= 0
  void enqueue(const uint8_t *ip, int16_t currentOperandStackSize);

  void verifyIp(int32_t ioffset);
  void verifyString(int32_t offset);

  int32_t ioffsetOf(const uint8_t *ip) { return ip - file.getCode(); }
  InstInfo *instInfoOf(const uint8_t *ip) {
    return instInfo.get() + ioffsetOf(ip);
  }

private:
  ByteFile &file;
  std::unique_ptr<InstInfo[]> instInfo;
  std::vector<const uint8_t *> stack;
};

} // namespace

Verifier::Verifier(ByteFile &file) : file(file) {
  instInfo = std::unique_ptr<InstInfo[]>(new InstInfo[file.getCodeSizeBytes()]);
}

void Verifier::verifyString(int32_t offset) {
  if (offset < 0 || offset >= file.getStringTableSize()) {
    invalidByteFileError("invalid string with out-of-bounds address {:#x}",
                         offset);
  }
}

void Verifier::verifyIp(int32_t ioffset) {
  if (ioffset < 0 || ioffset >= file.getCodeSizeBytes()) {
    invalidByteFileError("invalid code address {:#x} out of bounds [0, {:#x}]",
                         ioffset, file.getCodeSizeBytes());
  }
}

void Verifier::analyzeAt(const uint8_t *ip) {
  InstInfo *info = instInfoOf(ip);
  assert(info->visited);
  assert(info->operandStackSize);
}

const uint8_t *Verifier::lookUpIp(int32_t ioffset) {
  if (ioffset < 0 || ioffset >= file.getCodeSizeBytes()) {
    runtimeError("invalid code address {:#x} out of bounds [0, {:#x}]", ioffset,
                 file.getCodeSizeBytes());
  }
  return file.getCode() + ioffset;
}

void Verifier::enqueue(const uint8_t *ip, int16_t currentOperandStackSize) {
  InstInfo *info = instInfoOf(ip);
  info->rift = true;
  if (info->visited) {
    if (info->operandStackSize != currentOperandStackSize) {
      runtimeError("operand stack size inconsistency at {:#x}", ioffsetOf(ip));
    }
    return;
  }
  info->visited = true;
  info->operandStackSize = currentOperandStackSize;
  stack.push_back(ip);
}

void Verifier::analyzeControlFlow() {
  const int32_t *symtab = file.getPublicSymbolTable();
  for (int i = 0; i < file.getPublicSymbolNum(); ++i) {
    int32_t ioffset = symtab[2 * i + 1];
    const uint8_t *ip = lookUpIp(ioffset);
    enqueue(ip, /*currentOperandStackSize=*/0);
  }
  while (!stack.empty()) {
    const uint8_t *ip = stack.back();
    stack.pop_back();
    analyzeAt(ip);
  }
}

void Verifier::verifyPublicSymTab() {
  const int32_t *symtab = file.getPublicSymbolTable();
  for (int i = 0; i < file.getPublicSymbolNum(); ++i) {
    try {
      int32_t nameOffset = symtab[2 * i];
      verifyString(nameOffset);
      int32_t ioffset = symtab[2 * i + 1];
      verifyIp(ioffset);
    } catch (InvalidByteFileError &e) {
      invalidByteFileError("invalid public symbol {}: {}", i, e.what());
    }
  }
}

void Verifier::verifyStringTable() {
  if (file.getStringTableSize() == 0)
    invalidByteFileError("empty string table");
  char lastChar = file.getStringTable()[file.getStringTableSize() - 1];
  if (lastChar != '\0') {
    invalidByteFileError("string table ends with non-zero char {:#x}",
                         lastChar);
  }
}

void Verifier::verify() {
  verifyStringTable();
  verifyPublicSymTab();
  invalidByteFileError("NOT IMPLEMENTED");
}

void lama::verify(ByteFile &file) { Verifier(file).verify(); }
