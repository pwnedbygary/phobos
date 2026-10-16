//The PSP system's tests: ares/psp beyond the CPU (which tests/allegrex covers), built on the host the same way.
#include "system.hpp"

#include <cstdlib>
#include <string>

int main() {
  using namespace allegrex_test;
  using namespace allegrex_test::psp;
  Tests tests = memoryTests();
  for(auto& test : loaderTests()) tests.push_back(test);
  for(auto& test : kernelTests()) tests.push_back(test);
  for(auto& test : callbackTests()) tests.push_back(test);
  for(auto& test : powerTests()) tests.push_back(test);
  for(auto& test : audioTests()) tests.push_back(test);
  for(auto& test : sasTests()) tests.push_back(test);
  for(auto& test : utilityTests()) tests.push_back(test);
  for(auto& test : dialogTests()) tests.push_back(test);
  for(auto& test : poolTests()) tests.push_back(test);
  for(auto& test : messageTests()) tests.push_back(test);
  for(auto& test : mutexTests()) tests.push_back(test);
  for(auto& test : threadmanTests()) tests.push_back(test);
  for(auto& test : spinTests()) tests.push_back(test);
  for(auto& test : timerTests()) tests.push_back(test);
  for(auto& test : mediaTests()) tests.push_back(test);
  for(auto& test : atracTests()) tests.push_back(test);
  for(auto& test : mp3Tests()) tests.push_back(test);
  for(auto& test : movieTests()) tests.push_back(test);
  for(auto& test : psmfTests()) tests.push_back(test);
  for(auto& test : fontTests()) tests.push_back(test);
  for(auto& test : cccTests()) tests.push_back(test);
  for(auto& test : jpegTests()) tests.push_back(test);
  for(auto& test : fileTests()) tests.push_back(test);
  for(auto& test : asyncTests()) tests.push_back(test);
  for(auto& test : discTests()) tests.push_back(test);
  for(auto& test : discFormatTests()) tests.push_back(test);
  for(auto& test : discInfoTests()) tests.push_back(test);
  for(auto& test : desktopDiscInfoTests()) tests.push_back(test);
  for(auto& test : cryptoTests()) tests.push_back(test);
  for(auto& test : decryptTests()) tests.push_back(test);
  for(auto& test : pgdTests()) tests.push_back(test);
  for(auto& test : moduleTests()) tests.push_back(test);
  for(auto& test : stateTests()) tests.push_back(test);
  for(auto& test : geTests()) tests.push_back(test);
  for(auto& test : drawTests()) tests.push_back(test);
  for(auto& test : draw3dTests()) tests.push_back(test);
  for(auto& test : curvesTests()) tests.push_back(test);
  for(auto& test : measureTests()) tests.push_back(test);
  for(auto& test : measured6Tests()) tests.push_back(test);
  for(auto& test : gpuTests()) tests.push_back(test);
  checkRequired();
  //(PHOBOS_TESTS_ONLY: only the tests whose names have it in them, for a quicker run while one is worked on)
  const char* only = std::getenv("PHOBOS_TESTS_ONLY");
  if(only && *only) std::erase_if(tests, [&](auto& test) { return std::string{test.first}.find(only) == std::string::npos; });
  for(auto& [name, run] : tests) {
    currentTest = name;
    int before = failures;
    run();
    report(before);
  }
  summary(tests.size());
  return failures ? 1 : 0;
}
