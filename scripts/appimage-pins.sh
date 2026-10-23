# shellcheck shell=bash
# What the Linux AppImage is made with, each a fixed build named by its URL and checked by its SHA-256: read by
# scripts/package-linux-appimage.sh, which downloads them, and scripts/check-appimage-runtime.sh. Where a list of
# URLs is given, they are tried in turn and every one must give the same file.
# shellcheck disable=SC2034
GITHUB=https://github.com

# The tools, release builds (each project's "continuous" build is replaced whenever it's built again). Neither ships
# in the AppImage: linuxdeploy fills its folder, appimagetool makes the image and puts the runtime in front of it.
LINUXDEPLOY_URL=$GITHUB/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
APPIMAGETOOL_URL=$GITHUB/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

# The runtime, the start of every AppImage (LICENSE, "AppImage runtime"): type2-runtime's build of RUNTIME_COMMIT.
# type2-runtime publishes it only as its continuous build, replaced at its next commit, so a copy is kept as an
# asset of Phobos's v2.4.0 release, and type2-runtime's continuous build is tried after it. Its sections
# appimagetool fills in (.digest_md5, .upd_info, .sha256_sig and .sig_key) are zeros in this file.
RUNTIME_COMMIT=fedfad51acc2860b2916a1eef32ba4a500ff31ef
RUNTIME_FILE=type2-runtime-${RUNTIME_COMMIT:0:9}-x86_64
RUNTIME_SHA256=617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46
RUNTIME_URLS=(
  "$GITHUB/pwnedbygary/phobos/releases/download/v2.4.0/$RUNTIME_FILE"
  "$GITHUB/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
)
# The versions LICENSE's notice gives for the libraries the runtime links, which check-appimage-runtime.sh finds in it.
LIBFUSE_VERSION=3.15.0
SQUASHFUSE_VERSION=0.5.2

# The runtime links libfuse (LGPL 2.1) statically, so its source goes beside each AppImage, as the runtime was built:
# the tarball type2-runtime's scripts/common/install-dependencies.sh downloads and checks at RUNTIME_COMMIT, the patch
# it applies, and type2-runtime's own source at that commit, to build the runtime again with a changed libfuse.
LIBFUSE_SOURCE=fuse-$LIBFUSE_VERSION.tar.xz
LIBFUSE_SOURCE_URL=$GITHUB/libfuse/libfuse/releases/download/fuse-$LIBFUSE_VERSION/$LIBFUSE_SOURCE
LIBFUSE_SOURCE_SHA256=70589cfd5e1cff7ccd6ac91c86c01be340b227285c5e200baa284e401eea2ca0
LIBFUSE_PATCH=fuse-$LIBFUSE_VERSION-type2-runtime-mount.c.diff
LIBFUSE_PATCH_URL=https://raw.githubusercontent.com/AppImage/type2-runtime/$RUNTIME_COMMIT/patches/libfuse/mount.c.diff
LIBFUSE_PATCH_SHA256=1c7fd9e26717545a476b226b083a9f9d05676c180edbd71a04bbd8a73599dc44
RUNTIME_SOURCE=type2-runtime-$RUNTIME_COMMIT.tar.gz
RUNTIME_SOURCE_SHA256=e27ea3c837e9c660e1a9a2993b673ae93bbb53077330efba8654031e302d4e8e
RUNTIME_SOURCE_URLS=(
  "$GITHUB/pwnedbygary/phobos/releases/download/v2.4.0/$RUNTIME_SOURCE"
  "$GITHUB/AppImage/type2-runtime/archive/$RUNTIME_COMMIT.tar.gz"
)
