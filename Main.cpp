#include "ByteFile.h"
#include "Interpreter.h"
#include "Verifier.h"
#include "fmt/format.h"
#include <iostream>

using namespace lama;

int main(int argc, const char **argv) {
  if (argc != 2) {
    std::cerr << "Please provide one argument: path to bytecode file"
              << std::endl;
    return 1;
  }
  std::string byteFilePath = argv[1];
  try {
    ByteFile byteFile = ByteFile::load(byteFilePath);
    verify(byteFile);
    interpret(byteFile);
  } catch (InvalidByteFileError &e) {
    std::cerr << fmt::format("invalid bytefile at {}:", byteFilePath)
              << std::endl;
    std::cerr << e.what() << std::endl;
    return 2;
  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }
  return 0;
}
