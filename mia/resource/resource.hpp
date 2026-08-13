#pragma once
#include <span>
#include <cstdint>
namespace mia {
  struct DummyResource {
    operator std::span<const uint8_t>() const { return {}; }
    operator const void*() const { return nullptr; }
  };
  namespace Resource {
    namespace GameBoy { inline DummyResource BootDMG1; }
    namespace GameBoyColor { inline DummyResource BootCGB0; }
    namespace GameBoyAdvance { inline DummyResource Boot; }
    namespace SuperFamicom {
      inline DummyResource Cx4, DSP1, DSP1B, DSP2, DSP3, DSP4;
      inline DummyResource ST010, ST011, ST018, SGB1, SGB2, SGB2Boot, IPLROM;
    }
    namespace MasterSystem { inline DummyResource Boot; }
    namespace MegaDrive { inline DummyResource TMSS, SVP; }
    namespace MegaCD { inline DummyResource Boot; }
    namespace Mega32X { inline DummyResource Vector, SH2BootM, SH2BootS; }
    namespace Nintendo64 { inline DummyResource PIFNTSC, PIFPAL, PIFSM5; }
    namespace PlayStation { inline DummyResource Boot; }
    namespace WonderSwan { inline DummyResource Boot; }
    namespace WonderSwanColor { inline DummyResource Boot; }
    namespace PocketChallengeV2 { inline DummyResource Boot; }
    namespace ZXSpectrum { inline DummyResource Boot, BIOS; }
    namespace ZXSpectrum128 { inline DummyResource Boot, BIOS, Sub; }
    namespace MSX { inline DummyResource Boot; }
    namespace MSX2 { inline DummyResource Boot; }
    namespace NeoGeo { inline DummyResource Boot; }
    namespace NeoGeoPocket { inline DummyResource Boot; }
    namespace NeoGeoPocketColor { inline DummyResource Boot; }
    namespace PCEngineCD { inline DummyResource Boot; }
  }
}
