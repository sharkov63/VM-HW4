#include "Verifier.h"
#include "ByteFile.h"
#include "Error.h"
#include "Inst.h"
#include <assert.h>
#include <cstdint>
#include <memory>
#include <vector>

using namespace lama;

static constexpr int32_t LAMA_INT_MIN = -(1 << 30);
static constexpr int32_t LAMA_INT_MAX = (1 << 30) - 1;
static constexpr int32_t LAMA_INT_WIDTH = 31;

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

  void parse();
  /// \pre #ip is valid
  /// \pre currentOperandStackSize >= 0
  void enqueue(const uint8_t *ip, int16_t currentOperandStackSize, bool jump);
  void parseAt(const uint8_t *ip);

  const uint8_t *lookUpIp(int32_t ioffset);
  const char *lookUpString(int32_t soffset);

  void verifyIp(int32_t ioffset);
  void verifyString(int32_t offset);

  int32_t ioffsetOf(const uint8_t *ip) { return ip - codeBegin; }
  InstInfo *instInfoOf(const uint8_t *ip) {
    return instInfo.get() + ioffsetOf(ip);
  }

  friend class InstParser;

private:
  ByteFile &file;
  std::unique_ptr<InstInfo[]> instInfo;
  std::vector<const uint8_t *> stack;
  const uint8_t *const codeBegin;
  const uint8_t *const codeEnd;
};

class InstParser {
public:
  InstParser(const uint8_t *ip, Verifier &verifier);

  /// \throws InvalidByteFileError
  void parse();

  const uint8_t *getJumpTarget() const noexcept { return jumpTarget; }
  bool doesStop() const noexcept { return stop; }
  const uint8_t *getNextIp() const noexcept { return ip; }
  int32_t getNextOperandStackSize() const noexcept {
    return currentOperandStackSize;
  }

private:
  uint8_t nextByte();
  int32_t nextSigned();
  const char *nextString();
  const uint8_t *nextLabel();

  void operandStackPop(int32_t k);
  void operandStackPush(int32_t k);

private:
  Verifier &verifier;
  const uint8_t *const beginIp;
  const uint8_t byte;
  const uint8_t high;
  const uint8_t low;
  InstInfo *const info;

  const uint8_t *ip;
  int32_t currentOperandStackSize;

  const uint8_t *jumpTarget = nullptr;
  bool stop = false;
};

} // namespace

InstParser::InstParser(const uint8_t *ip, Verifier &verifier)
    : verifier(verifier), beginIp(ip), ip(ip), byte(nextByte()),
      high((0xF0 & byte) >> 4), low(0x0F & byte), info(verifier.instInfoOf(ip)),
      currentOperandStackSize(info->operandStackSize) {}

void InstParser::operandStackPop(int32_t k) {
  assert(k >= 0);
  if (currentOperandStackSize < k) {
    invalidByteFileError("need at least {} operands in stack size, found {}", k,
                         currentOperandStackSize);
  }
  currentOperandStackSize -= k;
}

void InstParser::operandStackPush(int32_t k) {
  assert(k >= 0);
  if (currentOperandStackSize + k >= (1 << 16)) {
    invalidByteFileError("operand stack size overflow");
  }
  currentOperandStackSize += k;
}

const uint8_t *InstParser::nextLabel() {
  int32_t ioffset = nextSigned();
  return verifier.lookUpIp(ioffset);
}

const char *InstParser::nextString() {
  int32_t offset = nextSigned();
  return verifier.lookUpString(offset);
}

int32_t InstParser::nextSigned() {
  if (ip + 4 > verifier.codeEnd) {
    invalidByteFileError("unexpected bytecode end, expected a word");
  }
  int32_t word;
  memcpy(&word, ip, sizeof(word));
  ip += sizeof(word);
  return word;
}

uint8_t InstParser::nextByte() {
  if (ip >= verifier.codeEnd) {
    invalidByteFileError("unexpected bytecode end, expected a byte");
  }
  return *ip++;
}

void InstParser::parse() {
  switch (byte) {
  case I_BINOP_Add:
  case I_BINOP_Sub:
  case I_BINOP_Mul:
  case I_BINOP_Div:
  case I_BINOP_Mod:
  case I_BINOP_Lt:
  case I_BINOP_Leq:
  case I_BINOP_Gt:
  case I_BINOP_Geq:
  case I_BINOP_Eq:
  case I_BINOP_Neq:
  case I_BINOP_And:
  case I_BINOP_Or: {
    operandStackPop(2);
    return;
  }
  case I_CONST: {
    int32_t value = nextSigned();
    if (value < LAMA_INT_MIN || value >= LAMA_INT_MAX) {
      invalidByteFileError("invalid CONST of {} is out of bounds", value);
    }
    operandStackPush(1);
    return;
  }
  case I_STRING: {
    nextString();
    operandStackPush(1);
    return;
  }
  case I_SEXP: {
    const char *str = nextString();
    int32_t nargs = nextSigned();
    if (nargs < 0) {
      invalidByteFileError("invalid nargs {} in SEXP {}", nargs, str);
    }
    operandStackPop(nargs);
    operandStackPush(1);
    return;
  }
  case I_STA: {
    operandStackPop(3);
    operandStackPush(1);
    return;
  }
  case I_JMP: {
    const uint8_t *ip = nextLabel();
    jumpTarget = ip;
    stop = true;
    return;
  }
  case I_END: {
    stop = true;
    return;
  }
  case I_DROP: {
    operandStackPop(1);
    return;
  }
  case I_DUP: {
    operandStackPush(1);
    return;
  }
  case I_SWAP: {
    return;
  }
  case I_ELEM: {
    operandStackPop(2);
    operandStackPush(1);
    return;
  }
  case I_LD_Global:
  case I_LD_Local:
  case I_LD_Arg:
  case I_LD_Access: {
    // TODO
    return;
  }
  case I_LDA_Global:
  case I_LDA_Local:
  case I_LDA_Arg:
  case I_LDA_Access: {
    // TODO
    return;
  }
  case I_ST_Global:
  case I_ST_Local:
  case I_ST_Arg:
  case I_ST_Access: {
    // TODO
    return;
  }
  case I_CJMPz:
  case I_CJMPnz: {
    // TODO
    jumpTarget = nextLabel();
    operandStackPop(1);
    return;
  }
  case I_BEGIN:
  case I_BEGINcl: {
    // TODO function
    return;
  }
  case I_CLOSURE: {
    // TODO
    return;
  }
  case I_CALLC: {
    // TODO call
    return;
  }
  case I_CALL: {
    // TODO call
    return;
  }
  case I_TAG: {
    const char *str = nextString();
    int32_t nargs = nextSigned();
    if (nargs < 0) {
      invalidByteFileError("negative nargs {} in TAG {}", nargs, str);
    }
    operandStackPop(1);
    operandStackPush(1);
    return;
  }
  case I_ARRAY: {
    int32_t nelems = nextSigned();
    if (nelems < 0) {
      invalidByteFileError("negative nelems {} in ARRAY", nelems);
    }
    operandStackPop(1);
    operandStackPush(1);
    return;
  }
  case I_FAIL: {
    nextSigned();
    nextSigned();
    operandStackPush(1);
    stop = true;
    return;
  }
  case I_LINE: {
    nextSigned();
    return;
  }
  case I_PATT_StrCmp: {
    operandStackPop(2);
    operandStackPush(1);
    return;
  }
  case I_PATT_String:
  case I_PATT_Array:
  case I_PATT_Sexp:
  case I_PATT_Boxed:
  case I_PATT_UnBoxed:
  case I_PATT_Closure: {
    operandStackPop(1);
    operandStackPush(1);
    return;
  }
  case I_CALL_Lread: {
    operandStackPush(1);
    return;
  }
  case I_CALL_Lwrite:
  case I_CALL_Llength:
  case I_CALL_Lstring: {
    operandStackPop(1);
    operandStackPush(1);
    return;
  }
  case I_CALL_Barray: {
    int32_t nargs = nextSigned();
    if (nargs < 0) {
      invalidByteFileError("negative nargs value for Barray call", nargs);
    }
    operandStackPop(nargs);
    operandStackPush(1);
    return;
  }
  default: {
    invalidByteFileError("unsupported instruction code {:#04x}", byte);
  }
  }
}

Verifier::Verifier(ByteFile &file)
    : file(file), instInfo(new InstInfo[file.getCodeSizeBytes()]),
      codeBegin(file.getCode()),
      codeEnd(file.getCode() + file.getCodeSizeBytes()) {}

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

const char *Verifier::lookUpString(int32_t soffset) {
  verifyString(soffset);
  return file.getStringTable() + soffset;
}

const uint8_t *Verifier::lookUpIp(int32_t ioffset) {
  verifyIp(ioffset);
  return codeBegin + ioffset;
}

void Verifier::parseAt(const uint8_t *ip) {
  InstParser parser(ip, *this);
  if (parser.getJumpTarget()) {
    enqueue(parser.getJumpTarget(), parser.getNextOperandStackSize(),
            /*jump=*/true);
  }
  if (parser.doesStop())
    return;
  enqueue(parser.getNextIp(), parser.getNextOperandStackSize(), /*jump=*/false);
}

void Verifier::enqueue(const uint8_t *ip, int16_t currentOperandStackSize,
                       bool jump) {
  InstInfo *info = instInfoOf(ip);
  if (info->visited) {
    if (info->operandStackSize != currentOperandStackSize) {
      runtimeError(
          "operand stack size inconsistency at instruction {:#x}; {} vs. {}",
          ioffsetOf(ip), info->operandStackSize, currentOperandStackSize);
    }
    return;
  }
  info->visited = true;
  info->operandStackSize = currentOperandStackSize;
  stack.push_back(ip);
}

void Verifier::parse() {
  const int32_t *symtab = file.getPublicSymbolTable();
  for (int i = 0; i < file.getPublicSymbolNum(); ++i) {
    int32_t ioffset = symtab[2 * i + 1];
    const uint8_t *ip = lookUpIp(ioffset);
    enqueue(ip, /*currentOperandStackSize=*/0, /*jump=*/true);
  }
  while (!stack.empty()) {
    const uint8_t *ip = stack.back();
    stack.pop_back();
    try {
      parseAt(ip);
    } catch (InvalidByteFileError &e) {
      invalidByteFileError("failed to parse at instruction {:#x}: {}",
                           ioffsetOf(ip), e.what());
    }
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
  parse();
  invalidByteFileError("NOT IMPLEMENTED");
}

void lama::verify(ByteFile &file) { Verifier(file).verify(); }
