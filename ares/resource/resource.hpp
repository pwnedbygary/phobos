#pragma once
#include <cstdint>
#include <nall/nall.hpp>

namespace ares::Resource {
    inline static const nall::vector<uint8_t> Logo{};

    struct DummyImage {
        template<typename T> operator nall::vector<T>() const { return {}; }
        operator nall::vector<uint8_t>() const { return {}; }
        operator nall::string() const { return {}; }
        operator nall::image() const { return {}; }
    };

    namespace Sprite {
        inline static const DummyImage Auxiliary0{};
        inline static const DummyImage Auxiliary1{};
        inline static const DummyImage Auxiliary2{};
        inline static const DummyImage Headphones{};
        inline static const DummyImage Initialized{};
        inline static const DummyImage LowBattery{};
        inline static const DummyImage CrosshairGreen{};
        inline static const DummyImage CrosshairRed{};

        namespace SuperFamicom {
            inline static const DummyImage CrosshairGreen{};
            inline static const DummyImage CrosshairRed{};
        }
        namespace WonderSwan {
            inline static const DummyImage Auxiliary0{};
            inline static const DummyImage Auxiliary1{};
            inline static const DummyImage Auxiliary2{};
            inline static const DummyImage Headphones{};
            inline static const DummyImage Initialized{};
            inline static const DummyImage LowBattery{};
        }
    }
}
