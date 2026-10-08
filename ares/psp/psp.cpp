#include <ares/ares.hpp>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include "psp.hpp"

//started: 2026-10-03

//The whole core as one translation unit, as Phobos builds each core. The parts are also built one by one (the host
//tests do), so each includes what it needs itself.
#include <psp/cpu/allegrex.cpp>
#include <psp/memory/memory.cpp>
#include <psp/kernel/loader.cpp>
#include <psp/kernel/kernel.cpp>
#include <psp/ge/ge.cpp>
#include <psp/ge/gpu/gpu.cpp>
#include <psp/system/system.cpp>
