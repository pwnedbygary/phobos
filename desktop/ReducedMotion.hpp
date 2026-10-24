#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace phobos::desktop {

// The motion settings a Linux desktop gives through its portal (org.freedesktop.portal.Settings), each empty where it
// gives none.
struct PortalMotionSettings {
  std::optional<std::uint32_t> reducedMotion;          // org.freedesktop.appearance reduced-motion: 1 asks for less
  std::optional<bool> enableAnimations;                // org.gnome.desktop.interface enable-animations
  std::optional<std::string> animationDurationFactor;  // org.kde.kdeglobals.KDE AnimationDurationFactor, as text
};

// Whether those settings ask for less motion: the standard key asking for it, GNOME's animations off, or KDE's
// animations at a speed factor of 0.
auto asksForLessMotion(const PortalMotionSettings& settings) -> bool;

// Whether the system asks apps for less motion: Windows' animation effects off, macOS's Reduce motion on, or a Linux
// desktop's portal settings (above). False where it can't tell.
auto systemAsksForLessMotion() -> bool;

}
