#include "ByteFile.h"
#include "Interpreter.h"
#include <iostream>

using namespace lama;

int main(int argc, const char **argv) {
  if (argc != 2) {
    std::cerr << "Please provide one argument: path to bytecode file"
              << std::endl;
    return -1;
  }
  std::string byteFilePath = argv[1];
  try {
    ByteFile byteFile = ByteFile::load(byteFilePath);
    interpret(std::move(byteFile));
  } catch (std::runtime_error &error) {
    std::cerr << error.what() << std::endl;
    return -1;
  }
  return 0;
}
