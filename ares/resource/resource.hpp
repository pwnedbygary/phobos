#pragma once
#include <nall/nall.hpp>
namespace nall { template<typename T> struct vector; struct string; struct image; }
namespace ares::Resource {
  inline static const nall::vector<uint8_t>* Logo = nullptr;
  struct DummyImage {
    template<typename T> operator nall::vector<T>() const { return {}; }
    operator nall::string() const { return {}; } operator nall::image() const { return {}; }
  };
  namespace Sprite {
    namespace WonderSwan {
      inline DummyImage Auxiliary0, Auxiliary1, Auxiliary2;
      inline DummyImage Headphones, Initialized, LowBattery;
      inline DummyImage Orientation0, Orientation1, PoweredOn, Sleeping;
      inline DummyImage VolumeA0, VolumeA1, VolumeA2;
      inline DummyImage VolumeB0, VolumeB1, VolumeB2, VolumeB3;
    }
    namespace SuperFamicom { inline DummyImage CrosshairRed, CrosshairGreen, CrosshairBlue; }
  }
}
