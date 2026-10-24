#include "ReducedMotion.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#elif defined(__linux__)
#include <dlfcn.h>

#include <chrono>
#include <cstdlib>
#include <type_traits>
#endif

namespace phobos::desktop {

namespace {

// KDE's factor is a decimal number as text ("0.25"); only a zero, however it's written, turns animations off.
auto isZero(const std::string& text) -> bool {
  bool digit = false, point = false;
  for (char c : text) {
    if (c == '0') {
      digit = true;
    } else if ((c == '.' || c == ',') && !point) {
      point = true;
    } else {
      return false;
    }
  }
  return digit;
}

}

auto asksForLessMotion(const PortalMotionSettings& settings) -> bool {
  return settings.reducedMotion == 1u || settings.enableAnimations == false ||
         (settings.animationDurationFactor && isZero(*settings.animationDurationFactor));
}

#if defined(_WIN32)

#ifndef SPI_GETCLIENTAREAANIMATION
#define SPI_GETCLIENTAREAANIMATION 0x1042
#endif

auto systemAsksForLessMotion() -> bool {
  BOOL animations = TRUE;
  return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations;
}

#elif defined(__APPLE__)

// AppKit through the Objective-C runtime, from C++: NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion.
// objc_msgSend must be called through a cast to the method's own type.
auto systemAsksForLessMotion() -> bool {
  id workspaceClass = reinterpret_cast<id>(objc_getClass("NSWorkspace"));
  if (!workspaceClass) return false;
  id workspace = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(workspaceClass, sel_registerName("sharedWorkspace"));
  if (!workspace) return false;
  SEL reduceMotion = sel_registerName("accessibilityDisplayShouldReduceMotion");
  return reinterpret_cast<BOOL (*)(id, SEL)>(objc_msgSend)(workspace, reduceMotion);
}

#elif defined(__linux__)

namespace {

// libdbus, loaded at run time as SDL loads it, so the program neither needs its headers to build nor the library to
// run: the dozen calls the portal's settings take, its types and constants declared here as its ABI has them.
struct DBusConnection;
struct DBusMessage;
struct DBusMessageIter {
  void* opaque[16];  // libdbus's own is 72 bytes on a 64-bit system
};
constexpr int busSession = 0;
constexpr int typeInvalid = 0, typeString = 's', typeVariant = 'v', typeUint32 = 'u', typeBoolean = 'b';
constexpr int callTimeoutMs = 300;

struct DBus {
  DBusConnection* (*busGetPrivate)(int, void*) = nullptr;
  void (*setExitOnDisconnect)(DBusConnection*, std::uint32_t) = nullptr;
  DBusMessage* (*newMethodCall)(const char*, const char*, const char*, const char*) = nullptr;
  std::uint32_t (*appendArgs)(DBusMessage*, int, ...) = nullptr;
  DBusMessage* (*sendWithReplyAndBlock)(DBusConnection*, DBusMessage*, int, void*) = nullptr;
  std::uint32_t (*iterInit)(DBusMessage*, DBusMessageIter*) = nullptr;
  int (*iterGetArgType)(DBusMessageIter*) = nullptr;
  void (*iterRecurse)(DBusMessageIter*, DBusMessageIter*) = nullptr;
  void (*iterGetBasic)(DBusMessageIter*, void*) = nullptr;
  void (*messageUnref)(DBusMessage*) = nullptr;
  void (*connectionClose)(DBusConnection*) = nullptr;
  void (*connectionUnref)(DBusConnection*) = nullptr;

  auto load() -> bool {
    void* library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    auto get = [library](auto& function, const char* name) {
      function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
      return function != nullptr;
    };
    // left loaded: SDL keeps its own handle on it too
    return get(busGetPrivate, "dbus_bus_get_private") &&
           get(setExitOnDisconnect, "dbus_connection_set_exit_on_disconnect") &&
           get(newMethodCall, "dbus_message_new_method_call") && get(appendArgs, "dbus_message_append_args") &&
           get(sendWithReplyAndBlock, "dbus_connection_send_with_reply_and_block") &&
           get(iterInit, "dbus_message_iter_init") && get(iterGetArgType, "dbus_message_iter_get_arg_type") &&
           get(iterRecurse, "dbus_message_iter_recurse") && get(iterGetBasic, "dbus_message_iter_get_basic") &&
           get(messageUnref, "dbus_message_unref") && get(connectionClose, "dbus_connection_close") &&
           get(connectionUnref, "dbus_connection_unref");
  }
};

// A setting's value as the portal gives it: its D-Bus type and the value.
struct Value {
  int type = typeInvalid;
  std::uint32_t number = 0;
  std::string text;
};

enum class Answer { Value, None, TimedOut };

// One setting through the portal's Read, which every version of it has, the value inside one variant or two.
auto read(DBus& dbus, DBusConnection* bus, const char* group, const char* key, Value& value) -> Answer {
  DBusMessage* call = dbus.newMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                         "org.freedesktop.portal.Settings", "Read");
  if (!call) return Answer::None;
  if (!dbus.appendArgs(call, typeString, &group, typeString, &key, typeInvalid)) {
    dbus.messageUnref(call);
    return Answer::None;
  }
  auto start = std::chrono::steady_clock::now();
  DBusMessage* reply = dbus.sendWithReplyAndBlock(bus, call, callTimeoutMs, nullptr);
  dbus.messageUnref(call);
  if (!reply) {
    // an error reply comes at once; no reply at all, after the whole timeout
    auto waited = std::chrono::steady_clock::now() - start;
    return waited >= std::chrono::milliseconds(callTimeoutMs * 9 / 10) ? Answer::TimedOut : Answer::None;
  }
  DBusMessageIter iters[3];
  int depth = 0;
  Answer answer = Answer::None;
  if (dbus.iterInit(reply, &iters[0])) {
    while (depth < 2 && dbus.iterGetArgType(&iters[depth]) == typeVariant) {
      dbus.iterRecurse(&iters[depth], &iters[depth + 1]);
      depth++;
    }
    DBusMessageIter& iter = iters[depth];
    value.type = dbus.iterGetArgType(&iter);
    if (value.type == typeUint32 || value.type == typeBoolean) {
      dbus.iterGetBasic(&iter, &value.number);
      answer = Answer::Value;
    } else if (value.type == typeString) {
      const char* text = nullptr;
      dbus.iterGetBasic(&iter, &text);
      value.text = text ? text : "";
      answer = Answer::Value;
    }
  }
  dbus.messageUnref(reply);
  return answer;
}

auto portalMotionSettings() -> PortalMotionSettings {
  PortalMotionSettings settings;
  // No session bus to ask: libdbus would start one for an X display (a virtual one, say) rather than answer no.
  if (!std::getenv("DBUS_SESSION_BUS_ADDRESS") && !std::getenv("XDG_RUNTIME_DIR")) return settings;
  DBus dbus;
  if (!dbus.load()) return settings;
  DBusConnection* bus = dbus.busGetPrivate(busSession, nullptr);
  if (!bus) return settings;
  dbus.setExitOnDisconnect(bus, 0);
  // each read stops the rest once one gets no answer in time, so a portal that hangs costs one timeout
  Value value;
  Answer answer = read(dbus, bus, "org.freedesktop.appearance", "reduced-motion", value);
  if (answer == Answer::Value && value.type == typeUint32) settings.reducedMotion = value.number;
  if (answer != Answer::TimedOut) {
    value = {};
    answer = read(dbus, bus, "org.gnome.desktop.interface", "enable-animations", value);
    if (answer == Answer::Value && value.type == typeBoolean) settings.enableAnimations = value.number != 0;
  }
  if (answer != Answer::TimedOut) {
    value = {};
    answer = read(dbus, bus, "org.kde.kdeglobals.KDE", "AnimationDurationFactor", value);
    if (answer == Answer::Value && value.type == typeString) settings.animationDurationFactor = value.text;
  }
  dbus.connectionClose(bus);
  dbus.connectionUnref(bus);
  return settings;
}

}

auto systemAsksForLessMotion() -> bool {
  return asksForLessMotion(portalMotionSettings());
}

#else

auto systemAsksForLessMotion() -> bool {
  return false;
}

#endif

}
