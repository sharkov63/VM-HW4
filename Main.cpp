#include "ByteFile.h"
#include "Interpreter.h"
#include "Verifier.h"
#include "fmt/chrono.h"
#include "fmt/format.h"
#include <chrono>
#include <iostream>

using namespace lama;

int main(int argc, const char **argv) {
  if (argc != 2) {
    std::cerr << "Please provide one argument: path to bytecode file"
              << std::endl;
    return 1;
  }
  auto startTime = std::chrono::steady_clock::now();
  std::string byteFilePath = argv[1];
  try {
    ByteFile byteFile = ByteFile::load(byteFilePath);
    verify(byteFile);
    std::cerr << "finished verification" << std::endl;
    auto verifiedTime = std::chrono::steady_clock::now();
    auto verificationDuration = verifiedTime - startTime;
    interpret(byteFile);
    auto finishedTime = std::chrono::steady_clock::now();
    auto interpretationDuration = finishedTime - verifiedTime;
    std::cerr << fmt::format("verification time: {:%S}", verificationDuration)
              << std::endl;
    std::cerr << fmt::format("interpretation time: {:%S}",
                             interpretationDuration)
              << std::endl;
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
