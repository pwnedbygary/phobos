//The PSP system's tests: ares/psp beyond the CPU (which tests/allegrex covers), built on the host the same way.
#include "system.hpp"

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
  for(auto& test : poolTests()) tests.push_back(test);
  for(auto& test : messageTests()) tests.push_back(test);
  for(auto& test : mediaTests()) tests.push_back(test);
  for(auto& test : atracTests()) tests.push_back(test);
  for(auto& test : mp3Tests()) tests.push_back(test);
  for(auto& test : fontTests()) tests.push_back(test);
  for(auto& test : fileTests()) tests.push_back(test);
  for(auto& test : asyncTests()) tests.push_back(test);
  for(auto& test : discTests()) tests.push_back(test);
  for(auto& test : discFormatTests()) tests.push_back(test);
  for(auto& test : cryptoTests()) tests.push_back(test);
  for(auto& test : decryptTests()) tests.push_back(test);
  for(auto& test : moduleTests()) tests.push_back(test);
  for(auto& test : stateTests()) tests.push_back(test);
  for(auto& test : geTests()) tests.push_back(test);
  for(auto& test : drawTests()) tests.push_back(test);
  for(auto& test : draw3dTests()) tests.push_back(test);
  for(auto& test : measureTests()) tests.push_back(test);
  for(auto& [name, run] : tests) {
    currentTest = name;
    int before = failures;
    run();
    std::printf("%s %s\n", failures == before ? "pass" : "FAIL", currentTest);
  }
  std::printf("%zu groups, %d failure%s\n", tests.size(), failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
