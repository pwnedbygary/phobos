#pragma once
// Desktop build of Android's logging call: the cores, mia and the runner log through
// __android_log_print, which prints to stderr here. CMake puts this directory first on the
// include path and force-includes this header into the core sources that use the call
// without including <android/log.h>.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

enum {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT,
};

// Debug and verbose messages print only when PHOBOS_DEBUG_LOG is set.
inline int __android_log_vprint(int priority, const char* tag, const char* format, std::va_list args) {
  static const bool debug = std::getenv("PHOBOS_DEBUG_LOG") != nullptr;
  if (priority < ANDROID_LOG_INFO && !debug) return 0;
  char line[2048];
  int prefix = std::snprintf(line, sizeof(line), "[%s] ", tag ? tag : "Phobos");
  if (prefix < 0 || prefix >= (int)sizeof(line)) prefix = 0;
  std::vsnprintf(line + prefix, sizeof(line) - prefix, format, args);
  std::fprintf(stderr, "%s\n", line);
  return 1;
}

inline int __android_log_print(int priority, const char* tag, const char* format, ...) {
  std::va_list args;
  va_start(args, format);
  int result = __android_log_vprint(priority, tag, format, args);
  va_end(args);
  return result;
}
