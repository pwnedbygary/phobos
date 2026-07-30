#include <ares/ares.hpp>

namespace ares {

atomic<bool> _runAhead = false;
Scheduler scheduler;

const string Name       = "ares";
const string Version    = "143";
const string Copyright  = "ares team";
const string License    = "ISC";
const string LicenseURI = "https://opensource.org/licenses/ISC";
const string Website    = "ares-emulator.github.io";
const string WebsiteURI = "https://ares-emulator.github.io/";
const u32    SerializerSignature = 0x31545342;  //"BST1" (little-endian)

}
