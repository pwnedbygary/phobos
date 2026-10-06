//The PSP system's tests: ares/psp beyond the CPU (which tests/allegrex covers), built on the host the same way.
#include "system.hpp"

int main() {
  using namespace allegrex_test;
  using namespace allegrex_test::psp;
  Tests tests = memoryTests();
  for(auto& test : loaderTests()) tests.push_back(test);
  for(auto& [name, run] : tests) {
    currentTest = name;
    int before = failures;
    run();
    std::printf("%s %s\n", failures == before ? "pass" : "FAIL", currentTest);
  }
  std::printf("%zu groups, %d failure%s\n", tests.size(), failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
