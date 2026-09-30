#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

// The firmware ares bundles, as listed in resource.bml. Upstream ares generates this header and the
// data with its sourcery tool; the Android build writes the data from mia/Firmware at configure time
// (the root CMakeLists.txt).
namespace mia::Resource {
  struct Blob {
    const uint8_t* data;
    size_t size;
    operator std::span<const uint8_t>() const { return {data, size}; }
    operator const void*() const { return data; }
  };

  namespace GameBoy { extern const Blob BootDMG0, BootDMG1, BootMGB; }
  namespace GameBoyColor { extern const Blob BootCGB0, BootCGB1; }
  namespace MegaDrive { extern const Blob TMSS, SVP; }
  namespace Mega32X { extern const Blob Vector, SH2BootM, SH2BootS; }
  namespace Nintendo64 { extern const Blob CIC6101, CIC6102, CIC6105, CIC7101, PIFSM5, PIFNTSC, PIFPAL; }
  namespace SuperFamicom {
    extern const Blob IPLROM, Cx4, DSP1, DSP1B, DSP2, DSP3, DSP4, SGB1, SGB2, ST010, ST011, ST018, S21FX;
  }
  namespace WonderSwan { extern const Blob Boot; }
  namespace WonderSwanColor { extern const Blob Boot; }
  namespace PocketChallengeV2 { extern const Blob Boot; }
  namespace ZXSpectrum { extern const Blob BIOS; }
  namespace ZXSpectrum128 { extern const Blob BIOS, Sub; }
}
