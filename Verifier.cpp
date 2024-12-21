#include "Verifier.h"
#include "ByteFile.h"
#include "Inst.h"
#include "fmt/format.h"
#include <assert.h>
#include <cstdint>
#include <limits>
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

static constexpr int32_t II_REACHED = (1 << 0);

static constexpr int32_t FI_REACHED = (1 << 0);
static constexpr int32_t FI_IS_CLOSURE = (1 << 1);

namespace {

struct FunctionInfo {
  int8_t flags = 0;
  int16_t nclosurevars = 0;

  bool isReached() const noexcept { return flags & FI_REACHED; }
  void setReached() noexcept { flags |= FI_REACHED; }
  bool isClosure() const noexcept { return flags & FI_IS_CLOSURE; }
  bool isNonClosure() const noexcept { return !isClosure(); }
  void setClosure() noexcept { flags |= FI_IS_CLOSURE; }
  void setNonClosure() noexcept { flags &= ~FI_IS_CLOSURE; }
};

struct InstInfo {
  int8_t flags = 0;
  int8_t length;
  int16_t operandStackSize;

  bool isReached() const noexcept { return flags & II_REACHED; }
  void setReached() noexcept { flags |= II_REACHED; }
};

class Verifier {
public:
  Verifier(ByteFile &file);

  /// \throws InvalidByteFileError on invalid bytefile
  void verify();

  void augument() noexcept;

private:
  void verifyStringTable();
  /// \throws InvalidByteFileError on invalid public symbol table
  void verifyPublicSymTab();

  void parse();
  void parseFunction(const uint8_t *beginIp);

  void augumentFunction(const uint8_t *beginIp);

  void enqueuePublicSymbols();
  /// \pre #ip is valid
  /// \pre currentOperandStackSize >= 0
  void enqueueInst(const uint8_t *ip, int16_t currentOperandStackSize);
  /// \pre #ip is valid
  void enqueueFunction(const uint8_t *beginIp);
  /// \pre #ip is valid
  /// \pre nclosurevars >= 0
  void enqueueClosure(const uint8_t *beginIp, int16_t nclosurevars);

  void parseAt(const uint8_t *ip);

  const uint8_t *lookUpIp(int32_t ioffset);
  const char *lookUpString(int32_t soffset);

  void verifyIp(int32_t ioffset);
  void verifyString(int32_t offset);
  void verifyLocation(VarDesignation designation, int32_t index);

  /// \pre #ip is valid
  int32_t ioffsetOf(const uint8_t *ip) { return ip - codeBegin; }
  /// \pre #ip is valid
  InstInfo *instInfoOf(const uint8_t *ip) {
    return instInfo.get() + ioffsetOf(ip);
  }

  /// \pre #ip is valid and start of the function
  FunctionInfo *functionInfoOf(const uint8_t *beginIp) {
    return functionInfo.get() + ioffsetOf(beginIp);
  }

  friend class InstParser;

private:
  ByteFile &file;
  std::unique_ptr<InstInfo[]> instInfo;
  std::unique_ptr<FunctionInfo[]> functionInfo;
  std::vector<const uint8_t *> instStack;
  std::vector<const uint8_t *> functions;
  size_t nextFunctionIdx;
  const uint8_t *const codeBegin;
  const uint8_t *const codeEnd;

  struct {
    int32_t nargs;
    int32_t nlocals;
    int32_t nclosurevars;
  } currentFunction;
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
  const uint8_t *nextIp();
  VarDesignation nextDesignation();
  void nextLoc(VarDesignation designation);

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

void InstParser::nextLoc(VarDesignation designation) {
  int32_t index = nextSigned();
  verifier.verifyLocation(designation, index);
}

VarDesignation InstParser::nextDesignation() {
  uint8_t byte = nextByte();
  if (byte > LOC_Last) {
    invalidByteFileError("invalid variable designation {:#x}", byte);
  }
  return static_cast<VarDesignation>(byte);
}

const uint8_t *InstParser::nextIp() {
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
    const uint8_t *ip = nextIp();
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
    nextLoc(static_cast<VarDesignation>(low));
    operandStackPush(1);
    return;
  }
  case I_LDA_Global:
  case I_LDA_Local:
  case I_LDA_Arg:
  case I_LDA_Access: {
    nextLoc(static_cast<VarDesignation>(low));
    operandStackPush(2);
    return;
  }
  case I_ST_Global:
  case I_ST_Local:
  case I_ST_Arg:
  case I_ST_Access: {
    nextLoc(static_cast<VarDesignation>(low));
    return;
  }
  case I_CJMPz:
  case I_CJMPnz: {
    jumpTarget = nextIp();
    operandStackPop(1);
    return;
  }
  case I_BEGIN:
  case I_BEGINcl: {
    int32_t nargs = nextSigned();
    if (nargs < 0) {
      invalidByteFileError("negative nargs {} in (C)BEGIN", nargs);
    }
    if (nargs >= (1 << 16)) {
      invalidByteFileError("too large nargs {} >= 2^16 in (C)BEGIN", nargs);
    }
    int32_t nlocals = nextSigned();
    if (nlocals < 0) {
      invalidByteFileError("negative nlocals {} in (C)BEGIN", nlocals);
    }
    verifier.currentFunction.nargs = nargs;
    verifier.currentFunction.nlocals = nlocals;
    return;
  }
  case I_CLOSURE: {
    const uint8_t *closureIp = nextIp();
    int32_t nclosurevars = nextSigned();
    if (nclosurevars < 0) {
      invalidByteFileError("negative closure vars num {} for CLOSURE {#:x}",
                           nclosurevars, verifier.ioffsetOf(closureIp));
    }
    for (int i = 0; i < nclosurevars; ++i) {
      VarDesignation designation = nextDesignation();
      nextLoc(designation);
    }
    verifier.enqueueClosure(closureIp, nclosurevars);
    operandStackPush(1);
    return;
  }
  case I_CALLC: {
    int32_t nargs = nextSigned();
    if (nargs < 0) {
      invalidByteFileError("negative nargs {} in CALLC", nargs);
    }
    operandStackPop(nargs + 1);
    operandStackPush(1);
    return;
  }
  case I_CALL: {
    const uint8_t *functionIp = nextIp();
    int32_t nargs = nextSigned();
    if (nargs < 0) {
      invalidByteFileError("negative nargs {} in CALL", nargs);
    }
    verifier.enqueueFunction(functionIp);
    operandStackPop(nargs);
    operandStackPush(1);
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
      functionInfo(new FunctionInfo[file.getCodeSizeBytes()]),
      codeBegin(file.getCode()),
      codeEnd(file.getCode() + file.getCodeSizeBytes()) {}

void Verifier::verifyLocation(VarDesignation designation, int32_t index) {
  if (index < 0) {
    invalidByteFileError("negative location index {}", index);
  }
  switch (designation) {
  case LOC_Global: {
    if (index >= file.getGlobalAreaSize()) {
      invalidByteFileError("global variable at index {} is out-of-bounds {}",
                           index, file.getGlobalAreaSize());
    }
    break;
  }
  case LOC_Local: {
    if (index >= currentFunction.nlocals) {
      invalidByteFileError("local variable at index {} is out-of-bounds {}",
                           index, currentFunction.nlocals);
    }
    break;
  }
  case LOC_Arg: {
    if (index >= currentFunction.nargs) {
      invalidByteFileError("argument at index {} is out-of-bounds {}", index,
                           currentFunction.nargs);
    }
    break;
  }
  case LOC_Access: {
    if (index >= currentFunction.nclosurevars) {
      invalidByteFileError("closure variable at index {} is out-of-bounds {}",
                           index, currentFunction.nclosurevars);
    }
    break;
  }
  }
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
  parser.parse();
  int32_t length = parser.getNextIp() - ip;
  if (length >= std::numeric_limits<uint8_t>::max()) {
    invalidByteFileError("too large length {} of instruction at {:#x}", length,
                         ioffsetOf(ip));
  }
  instInfoOf(ip)->length = length;
  if (parser.getJumpTarget()) {
    enqueueInst(parser.getJumpTarget(), parser.getNextOperandStackSize());
  }
  if (!parser.doesStop()) {
    enqueueInst(parser.getNextIp(), parser.getNextOperandStackSize());
  }
}

void Verifier::enqueueClosure(const uint8_t *beginIp, int16_t nclosurevars) {
  if (*beginIp != I_BEGINcl) {
    invalidByteFileError("a closure begins with bytecode {:#x}, "
                         "expected CBEGIN ({:#x})",
                         *beginIp, (int)I_BEGINcl);
  }
  FunctionInfo *info = functionInfoOf(beginIp);
  if (info->isReached()) {
    if (info->isNonClosure()) {
      invalidByteFileError("function at {:#x} is both closure and non-closure",
                           ioffsetOf(beginIp));
    }
    if (info->nclosurevars != nclosurevars) {
      invalidByteFileError(
          "inconsistent variable count ({} vs. {}) for closure at {:#x}",
          info->nclosurevars, nclosurevars, ioffsetOf(beginIp));
    }
    return;
  }
  info->setReached();
  info->setClosure();
  info->nclosurevars = nclosurevars;
  functions.push_back(beginIp);
}

void Verifier::enqueueFunction(const uint8_t *beginIp) {
  if (*beginIp != I_BEGIN) {
    invalidByteFileError("a (non-closure) function begins with bytecode {:#x}, "
                         "expected BEGIN ({:#x})",
                         *beginIp, (int)I_BEGIN);
  }
  FunctionInfo *info = functionInfoOf(beginIp);
  if (info->isReached()) {
    if (info->isClosure()) {
      invalidByteFileError("function at {:#x} is both closure and non-closure",
                           ioffsetOf(beginIp));
    }
    return;
  }
  info->setReached();
  info->setNonClosure();
  functions.push_back(beginIp);
}

void Verifier::enqueueInst(const uint8_t *ip, int16_t currentOperandStackSize) {
  if (*ip == I_BEGIN || *ip == I_BEGINcl) {
    invalidByteFileError("non-call reach to BEGIN/CBEGIN instruction at {:#x}",
                         ioffsetOf(ip));
  }
  InstInfo *info = instInfoOf(ip);
  if (info->isReached()) {
    if (info->operandStackSize != currentOperandStackSize) {
      invalidByteFileError(
          "operand stack size inconsistency at instruction {:#x}; {} vs. {}",
          ioffsetOf(ip), info->operandStackSize, currentOperandStackSize);
    }
    return;
  }
  info->setReached();
  info->operandStackSize = currentOperandStackSize;
  instStack.push_back(ip);
}

void Verifier::enqueuePublicSymbols() {
  const int32_t *symtab = file.getPublicSymbolTable();
  for (int i = 0; i < file.getPublicSymbolNum(); ++i) {
    int32_t ioffset = symtab[2 * i + 1];
    const uint8_t *ip = lookUpIp(ioffset);
    enqueueFunction(ip);
  }
}

void Verifier::augumentFunction(const uint8_t *beginIp) {
  const uint8_t *ip = beginIp;
  int16_t maxOperandStackSize = 0;
  while (*ip != I_END) {
    InstInfo *info = instInfoOf(ip);
    maxOperandStackSize = std::max(maxOperandStackSize, info->operandStackSize);
    ip += info->length;
    if (ip >= codeEnd) {
      invalidByteFileError("reached code end starting from function at {:#x}",
                           ioffsetOf(beginIp));
    }
  }
  int32_t nargs;
  memcpy(&nargs, beginIp + 1, sizeof(nargs));
  nargs |= ((int32_t)maxOperandStackSize << 16);
  memcpy(const_cast<uint8_t *>(beginIp) + 1, &nargs, sizeof(nargs));
}

void Verifier::parseFunction(const uint8_t *beginIp) {
  FunctionInfo *info = functionInfoOf(beginIp);
  if (info->isClosure())
    currentFunction.nclosurevars = info->nclosurevars;
  instStack.push_back(beginIp);
  while (!instStack.empty()) {
    const uint8_t *ip = instStack.back();
    instStack.pop_back();
    try {
      parseAt(ip);
    } catch (InvalidByteFileError &e) {
      invalidByteFileError("failed to parse at instruction {:#x}: {}",
                           ioffsetOf(ip), e.what());
    }
  }
}

void Verifier::parse() {
  while (nextFunctionIdx < functions.size()) {
    const uint8_t *beginIp = functions[nextFunctionIdx++];
    try {
      parseFunction(beginIp);
    } catch (InvalidByteFileError &e) {
      invalidByteFileError("in function {:#x}: {}", ioffsetOf(beginIp),
                           e.what());
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
}

void Verifier::augument() noexcept {
  for (const uint8_t *beginIp : functions)
    augumentFunction(beginIp);
}

void lama::verify(ByteFile &file) {
  Verifier verifier(file);
  verifier.verify();
  verifier.augument();
}
