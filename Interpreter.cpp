#include "Interpreter.h"
#include "ByteFile.h"
#include "Error.h"
#include "Inst.h"
#include "Value.h"
#include <algorithm>
#include <array>

using namespace lama;

extern "C" {

extern Value __start_custom_data;
extern Value __stop_custom_data;
extern Value *__gc_stack_top;
extern Value *__gc_stack_bottom;

void __gc_init();

extern Value Lread();
extern int32_t Lwrite(Value boxedInt);
extern int32_t Llength(void *p);
extern void *Lstring(void *p);

extern void *Belem(void *p, int i);
extern void *Bstring(void *cstr);
extern void *Bsta(void *v, int i, void *x);
extern void *Barray(int bn, ...);
extern void *Barray_(void *stack_top, int n);
extern int LtagHash(char *tagString);
extern void *Bsexp(int bn, ...);
extern void *Bsexp_(void *stack_top, int n);
extern int Btag(void *d, int t, int n);
[[noreturn]] extern void Bmatch_failure(void *v, char *fname, int line,
                                        int col);
extern void *Bclosure(int bn, void *entry, ...);
extern void *Bclosure_(void *stack_top, int n, void *entry);
extern int Bstring_patt(void *x, void *y);
extern int Bclosure_tag_patt(void *x);
extern int Bboxed_patt(void *x);
extern int Bunboxed_patt(void *x);
extern int Barray_tag_patt(void *x);
extern int Bstring_tag_patt(void *x);
extern int Bsexp_tag_patt(void *x);
extern int Barray_patt(void *d, int n);
}

static void initGlobalArea() {
  for (Value *p = &__start_custom_data; p < &__stop_custom_data; ++p)
    *p = 1;
}

static Value &accessGlobal(uint32_t index) {
  return (&__start_custom_data)[index];
}

#define STACK_SIZE (1 << 20)
#define FRAME_STACK_SIZE (1 << 16)

namespace {

struct Stack {

  static void init() {
    __gc_stack_bottom = data.end();
    frame.base = __gc_stack_bottom;
    // Two arguments to main: argc and argv
    __gc_stack_top = __gc_stack_bottom - 3;
    frame.operandStackBase = frame.base;
  }

  static size_t getOperandStackSize() {
    return frame.operandStackBase - top() - 1;
  }
  static bool isEmpty() { return frameStackSize == 0; }
  static bool isNotEmpty() { return !isEmpty(); }
  static Value getClosure();

  static Value &accessLocal(ssize_t index);
  static Value &accessArg(ssize_t index);

  static void allocateNOperands(size_t noperands) { top() -= noperands; }

  static void pushOperand(Value value) {
    *top() = value;
    --top();
  }
  static Value peakOperand() { return top()[1]; }
  static Value popOperand() {
    ++top();
    return *top();
  }
  static void popNOperands(size_t noperands) { top() += noperands; }

  static void pushIntOperand(int32_t operand) { pushOperand(boxInt(operand)); }
  static int32_t popIntOperand() {
    Value operand = popOperand();
    if (!valueIsInt(operand)) {
      runtimeError(
          "expected a (boxed) number at the operand stack top, found {:#x}",
          operand);
    }
    return unboxInt(operand);
  }

  static void beginFunction(size_t nargs, size_t nlocals);
  static const uint8_t *endFunction();

  static void setNextReturnAddress(const uint8_t *address) {
    nextReturnAddress = address;
  }
  static void setNextIsClosure(bool isClousre) { nextIsClosure = isClousre; }

  static Value *&top() { return __gc_stack_top; }

private:
  static std::array<Value, STACK_SIZE> data;

  struct Frame {
    Value *base;
    Value *top;
    size_t nargs;
    size_t nlocals;
    Value *operandStackBase;
    const uint8_t *returnAddress;
  };

  static Frame frame;
  static std::array<Frame, FRAME_STACK_SIZE> frameStack;
  static size_t frameStackSize;

  static const uint8_t *nextReturnAddress;
  static bool nextIsClosure;
};

} // namespace

std::array<Value, STACK_SIZE> Stack::data;

Stack::Frame Stack::frame;
std::array<Stack::Frame, FRAME_STACK_SIZE> Stack::frameStack;
size_t Stack::frameStackSize = 0;
const uint8_t *Stack::nextReturnAddress;
bool Stack::nextIsClosure;

Value Stack::getClosure() { return frame.base[frame.nargs]; }

Value &Stack::accessLocal(ssize_t index) { return frame.base[-index - 1]; }

Value &Stack::accessArg(ssize_t index) {
  return frame.base[frame.nargs - 1 - index];
}
void Stack::beginFunction(size_t rawNargs, size_t nlocals) {
  size_t nargs = rawNargs & ((1 << 16) - 1);
  size_t noperands = nargs + nextIsClosure;
  if (frameStackSize >= FRAME_STACK_SIZE) {
    runtimeError("frame stack size exhausted");
  }
  Value *newBase = top() + 1;
  frame.top = newBase + noperands - 1;
  frameStack[frameStackSize++] = frame;
  frame.base = newBase;
  top() = newBase - nlocals - 1;
  frame.nargs = nargs;
  frame.nlocals = nlocals;
  frame.operandStackBase = top() + 1;
  frame.returnAddress = nextReturnAddress;

  size_t neededOperandStackSize = (nargs >> 16) & ((1 << 16) - 1);
  if (top() + 1 - neededOperandStackSize < data.begin()) {
    runtimeError("might exhaust stack");
  }

  // Fill with some boxed values so that GC will skip these
  memset(top() + 1, 1, (char *)frame.base - (char *)(top() + 1));
}

const uint8_t *Stack::endFunction() {
  if (isEmpty()) {
    runtimeError("no function to end");
  }
  const uint8_t *returnAddress = frame.returnAddress;
  Value ret = peakOperand();
  frame = frameStack[--frameStackSize];
  top() = frame.top;
  pushOperand(ret);
  return returnAddress;
}

static Value renderToString(Value value) {
  return reinterpret_cast<Value>(Lstring(reinterpret_cast<void *>(value)));
}

static Value createString(const char *cstr) {
  return reinterpret_cast<Value>(Bstring(const_cast<char *>(cstr)));
}

static Value createArray(size_t nargs) {
  return reinterpret_cast<Value>(Barray_(Stack::top() + 1, nargs));
}

static Value createSexp(size_t nargs) {
  return reinterpret_cast<Value>(Bsexp_(Stack::top() + 1, nargs));
}

static Value createClosure(const uint8_t *entry, size_t nvars) {
  return reinterpret_cast<Value>(
      Bclosure_(Stack::top() + 1, nvars, const_cast<uint8_t *>(entry)));
}

static const char unknownFile[] = "<unknown file>";

namespace {

class Interpreter {
public:
  Interpreter() = default;
  Interpreter(ByteFile *byteFile);

  void run();

private:
  /// \return true to continue, false to stop
  bool step();

  char readByte();
  int32_t readWord();

  Value &accessVar(char designation, int32_t index);

  const uint8_t *getCode(int32_t address);
  const char *getString(int32_t offset);

private:
  ByteFile *byteFile;

  const uint8_t *instructionPointer;
  const uint8_t *codeEnd;
} interpreter;

} // namespace

Interpreter::Interpreter(ByteFile *byteFile)
    : byteFile(byteFile), instructionPointer(this->byteFile->getCode()),
      codeEnd(instructionPointer + this->byteFile->getCodeSizeBytes()) {}

const char *Interpreter::getString(int32_t offset) {
  return byteFile->getStringTable() + offset;
}

const uint8_t *Interpreter::getCode(int32_t address) {
  return byteFile->getCode() + address;
}

void Interpreter::run() {
  __gc_init();
  Stack::init();
  while (true) {
    const uint8_t *currentInstruction = instructionPointer;
    try {
      if (!step())
        return;
    } catch (std::runtime_error &e) {
      runtimeError("runtime error at {:#x}: {}",
                   currentInstruction - byteFile->getCode(), e.what());
    }
  }
}

bool Interpreter::step() {
  // std::cerr << fmt::format("interpreting at {:#x}\n",
  //                          instructionPointer - byteFile.getCode());
  // if (Stack::getOperandStackSize()) {
  //   std::cerr << fmt::format("operand stack:\n");
  //   for (int i = 0; i < Stack::getOperandStackSize(); ++i) {
  //     Value element = Stack::top()[i + 1];
  //     std::cerr << fmt::format("operand {}, raw {:#x}\n", i,
  //     (unsigned)element);
  //   }
  // }
  // std::cerr << fmt::format("stack region is ({}, {})\n",
  // fmt::ptr(__gc_stack_top), fmt::ptr(__gc_stack_bottom));
  unsigned char byte = readByte();
  unsigned char high = (0xF0 & byte) >> 4;
  unsigned char low = 0x0F & byte;
  switch (byte) {
  case I_BINOP_Eq: {
    Value rhs = Stack::popOperand();
    Value lhs = Stack::popOperand();
    Value result = boxInt(lhs == rhs);
    Stack::pushOperand(result);
    return true;
  }
  case I_BINOP_Add:
  case I_BINOP_Sub:
  case I_BINOP_Mul:
  case I_BINOP_Div:
  case I_BINOP_Mod:
  case I_BINOP_Lt:
  case I_BINOP_Leq:
  case I_BINOP_Gt:
  case I_BINOP_Geq:
  case I_BINOP_Neq:
  case I_BINOP_And:
  case I_BINOP_Or: {
    int32_t rhs = Stack::popIntOperand();
    int32_t lhs = Stack::popIntOperand();
    if ((low == I_BINOP_Div || low == I_BINOP_Mod) && rhs == 0)
      runtimeError("division by zero");
    int32_t result;
    switch (byte) {
#define CASE(code, op)                                                         \
  case code: {                                                                 \
    result = lhs op rhs;                                                       \
    break;                                                                     \
  }
      CASE(I_BINOP_Add, +)
      CASE(I_BINOP_Sub, -)
      CASE(I_BINOP_Mul, *)
      CASE(I_BINOP_Div, /)
      CASE(I_BINOP_Mod, %)
      CASE(I_BINOP_Lt, <)
      CASE(I_BINOP_Leq, <=)
      CASE(I_BINOP_Gt, >)
      CASE(I_BINOP_Geq, >=)
      CASE(I_BINOP_Neq, !=)
      CASE(I_BINOP_And, &&)
      CASE(I_BINOP_Or, ||)
#undef CASE
    default: {
      runtimeError("undefined binary operator with code {:x}", low);
    }
    }
    Stack::pushIntOperand(result);
    return true;
  }
  case I_CONST: {
    Stack::pushIntOperand(readWord());
    return true;
  }
  case I_STRING: {
    uint32_t offset = readWord();
    const char *cstr = getString(offset);
    Value string = createString(cstr);
    Stack::pushOperand(string);
    return true;
  }
  case I_SEXP: {
    Value stringOffset = readWord();
    uint32_t nargs = readWord();

    const char *string = getString(stringOffset);
    Value tagHash = LtagHash(const_cast<char *>(string));
    std::reverse(Stack::top() + 1, Stack::top() + nargs + 1);
    Stack::pushOperand(0);
    Value *base = Stack::top() + 1;
    for (int i = 0; i < nargs; ++i) {
      base[i] = base[i + 1];
    }
    base[nargs] = tagHash;

    Value sexp = createSexp(nargs);

    Stack::popNOperands(nargs + 1);
    Stack::pushOperand(sexp);
    return true;
  }
  case I_STA: {
    Value value = Stack::popOperand();
    Value index = Stack::popOperand();
    Value container = Stack::popOperand();
    Value result =
        reinterpret_cast<Value>(Bsta(reinterpret_cast<void *>(value), index,
                                     reinterpret_cast<void *>(container)));
    Stack::pushOperand(result);
    return true;
  }
  case I_JMP: {
    uint32_t offset = readWord();
    instructionPointer = getCode(offset);
    return true;
  }
  case I_END: {
    const uint8_t *returnAddress = Stack::endFunction();
    if (Stack::isEmpty())
      return false;
    instructionPointer = returnAddress;
    return true;
  }
  case I_DROP: {
    Stack::popOperand();
    return true;
  }
  case I_DUP: {
    Stack::pushOperand(Stack::peakOperand());
    return true;
  }
  case I_ELEM: {
    Value index = Stack::popOperand();
    Value container = Stack::popOperand();
    Value element = reinterpret_cast<Value>(
        Belem(reinterpret_cast<void *>(container), index));
    Stack::pushOperand(element);
    return true;
  }
  case I_LD_Global:
  case I_LD_Local:
  case I_LD_Arg:
  case I_LD_Access: {
    int32_t index = readWord();
    Value &var = accessVar(low, index);
    Stack::pushOperand(var);
    return true;
  }
  case I_LDA_Global:
  case I_LDA_Local:
  case I_LDA_Arg:
  case I_LDA_Access: {
    int32_t index = readWord();
    Value *address = &accessVar(low, index);
    Stack::pushOperand(reinterpret_cast<Value>(address));
    Stack::pushOperand(reinterpret_cast<Value>(address));
    return true;
  }
  case I_ST_Global:
  case I_ST_Local:
  case I_ST_Arg:
  case I_ST_Access: {
    int32_t index = readWord();
    Value &var = accessVar(low, index);
    Value operand = Stack::peakOperand();
    var = operand;
    return true;
  }
  case I_CJMPz:
  case I_CJMPnz: {
    uint32_t offset = readWord();
    bool boolValue = Stack::popIntOperand();
    if (boolValue == (bool)low)
      instructionPointer = getCode(offset);
    return true;
  }
  case I_BEGIN:
  case I_BEGINcl: {
    uint32_t rawNargs = readWord();
    uint32_t nlocals = readWord();
    bool isClosure = low == 0x3;
    Stack::beginFunction(rawNargs, nlocals);
    return true;
  }
  case I_CLOSURE: {
    uint32_t entryOffset = readWord();
    uint32_t n = readWord();

    const uint8_t *entry = getCode(entryOffset);

    Stack::allocateNOperands(n);
    for (int i = 0; i < n; ++i) {
      char designation = readByte();
      uint32_t index = readWord();
      Value value = accessVar(designation, index);
      Stack::top()[i + 1] = value;
    }

    Value closure = createClosure(entry, n);

    Stack::popNOperands(n);
    Stack::pushOperand(closure);
    return true;
  }
  case I_CALLC: {
    uint32_t nargs = readWord();
    Value closure = Stack::top()[nargs + 1];
    const uint8_t *entry = *reinterpret_cast<const uint8_t **>(closure);
    Stack::setNextReturnAddress(instructionPointer);
    Stack::setNextIsClosure(true);
    instructionPointer = entry;
    return true;
  }
  case I_CALL: {
    uint32_t offset = readWord();
    const uint8_t *address = getCode(offset);
    readWord();
    Stack::setNextReturnAddress(instructionPointer);
    Stack::setNextIsClosure(false);
    instructionPointer = address;
    return true;
  }
  case I_TAG: {
    uint32_t stringOffset = readWord();
    uint32_t nargs = readWord();
    const char *string = getString(stringOffset);
    Value tag = LtagHash(const_cast<char *>(string));
    Value target = Stack::popOperand();

    Value result = Btag((void *)target, tag, boxInt(nargs));

    Stack::pushOperand(result);
    return true;
  }
  case I_ARRAY: {
    uint32_t nelems = readWord();
    Value array = Stack::popOperand();
    Value result = Barray_patt(reinterpret_cast<void *>(array), boxInt(nelems));
    Stack::pushOperand(result);
    return true;
  }
  case I_FAIL: {
    uint32_t line = readWord();
    uint32_t col = readWord();
    Value v = Stack::popOperand();
    Bmatch_failure((void *)v, const_cast<char *>(unknownFile), line,
                   col); // noreturn
  }
  case I_LINE: {
    readWord();
    return true;
  }
  case I_PATT_StrCmp: {
    Value x = Stack::popOperand();
    Value y = Stack::popOperand();
    Value result =
        Bstring_patt(reinterpret_cast<void *>(x), reinterpret_cast<void *>(y));
    Stack::pushOperand(result);
    return true;
  }
  case I_PATT_String: {
    Value operand = Stack::popOperand();
    Value result = Bstring_tag_patt(reinterpret_cast<void *>(operand));
    Stack::pushOperand(result);
    return true;
  }
  case I_PATT_Array: {
    Value operand = Stack::popOperand();
    Value result = Barray_tag_patt(reinterpret_cast<void *>(operand));
    Stack::pushOperand(result);
    return true;
  }
  case I_PATT_Sexp: {
    Value operand = Stack::popOperand();
    Value result = Bsexp_tag_patt(reinterpret_cast<void *>(operand));
    Stack::pushOperand(result);
    return true;
  }
  case I_PATT_Boxed: {
    Value operand = Stack::popOperand();
    Value result = Bboxed_patt(reinterpret_cast<void *>(operand));
    Stack::pushOperand(result);
    return true;
  }
  case I_PATT_UnBoxed: {
    Value operand = Stack::popOperand();
    Value result = Bunboxed_patt(reinterpret_cast<void *>(operand));
    Stack::pushOperand(result);
    return true;
  }
  case I_PATT_Closure: {
    Value operand = Stack::popOperand();
    Value result = Bclosure_tag_patt(reinterpret_cast<void *>(operand));
    Stack::pushOperand(result);
    return true;
  }
  case I_CALL_Lread: {
    Stack::pushOperand(Lread());
    return true;
  }
  case I_CALL_Lwrite: {
    Lwrite(Stack::popOperand());
    Stack::pushIntOperand(0);
    return true;
  }
  case I_CALL_Llength: {
    Value string = Stack::popOperand();
    Value length = Llength(reinterpret_cast<void *>(string));
    Stack::pushOperand(length);
    return true;
  }
  case I_CALL_Lstring: {
    Value operand = Stack::popOperand();
    Value rendered = renderToString(operand);
    Stack::pushOperand(rendered);
    return true;
  }
  case I_CALL_Barray: {
    uint32_t nargs = readWord();
    std::reverse(Stack::top() + 1, Stack::top() + nargs + 1);
    Value array = createArray(nargs);
    Stack::popNOperands(nargs);
    Stack::pushOperand(array);
    return true;
  }
  }
  runtimeError("unsupported instruction code {:#04x}", byte);
}

char Interpreter::readByte() { return *instructionPointer++; }

int32_t Interpreter::readWord() {
  int32_t word;
  memcpy(&word, instructionPointer, sizeof(int32_t));
  instructionPointer += sizeof(int32_t);
  return word;
}

Value &Interpreter::accessVar(char designation, int32_t index) {
  switch (designation) {
  case LOC_Global:
    return accessGlobal(index);
  case LOC_Local:
    return Stack::accessLocal(index);
  case LOC_Arg:
    return Stack::accessArg(index);
  case LOC_Access:
    Value *closure = reinterpret_cast<Value *>(Stack::getClosure());
    return closure[index + 1];
  }
  runtimeError("unsupported variable designation {:#x}", designation);
}

void lama::interpret(ByteFile &byteFile) {
  initGlobalArea();
  interpreter = Interpreter(&byteFile);
  interpreter.run();
}
