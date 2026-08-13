<img src="https://github.com/pwnedbygary/phobos/blob/master/ares/ares/resource/logo%402x.png" width="350"/>

**Phobos** is a port of the well regarded Ares, a multi-system emulator that began development on October 14th, 2004.
It is a descendant of [higan](https://github.com/higan-emu/higan) and [bsnes](https://github.com/bsnes-emu/bsnes/), and focuses on accuracy and preservation.

It's worth noting that Ares takes some uncommon design approaches that essentially trade speed for code clarity. We avoid state machines and bitmasks (when possible). Most cores end up being half the amount of code, but slower. The code is clearer and less spaghettified, especially for systems with lots of processors. C bitfields being non-portable incurs a speedhit. Windows also has a speedhit over Linux due to its ABI needing more instructions to switch contexts.

Official Releases
-----------------

Phobos is currently **Android-only**. Release APKs (legacy + modern flavors)
are built automatically by GitHub Actions on every commit and published on the
[GitHub Releases](https://github.com/pwnedbygary/phobos/releases) page when a
version tag is pushed. Debug builds are not published.

Building Phobos
-------------

Requires the Android SDK (platform 37, build-tools 36, NDK 28.x, CMake 3.22.1)
and a JDK 17+. From the `android/` directory:

```sh
./gradlew assembleRelease        # release APKs: app-legacy-release.apk + app-modern-release.apk
./gradlew assembleDebug          # debug APKs (also builds all four variants)
```

APKs land in `app/build/outputs/apk/<flavor>/<type>/`.

High-level Components
---------------------

* __ares__:       emulator cores and component implementations
* __android__:    main GUI implementation written in Kotlin and C++ featuring a JNI bridge to the Ares cores.
* __nall__:       Near's alternative to the C++ standard library
* __mia__:        internal ROM database and ROM/image loader
* __libco__:      cooperative multithreading library

Contributing
------------

Please join my discord [[HERE]](https://discord.gg/EkSNHnmYma) if you have any questions/wish to contribute.
