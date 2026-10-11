cmake_minimum_required(VERSION 3.22)
project(phobos LANGUAGES C CXX)

# Android builds libphobos_android.so for the Gradle app. Everywhere else this builds the
# desktop program, phobos, which runs the same cores and runner under SDL.
set(PHOBOS_DEFINITIONS BUILD_RELEASE VULKAN VK_NO_PROTOTYPES MIA_LIBRARY SLJIT_HAVE_CONFIG_PRE=1 SLJIT_HAVE_CONFIG_POST=1 SLJIT_DEBUG=0 ARES_ENABLE_ZIP ARES_ENABLE_CHD CORE_A26 CORE_CV CORE_FC CORE_GB CORE_GBA CORE_MD CORE_MS CORE_MSX CORE_N64 CORE_NG CORE_NGP CORE_PCE CORE_PS1 CORE_PSP CORE_SATURN CORE_SFC CORE_SG CORE_SPEC CORE_WS PHOBOS_TAPE_SPEED PROFILE_PERFORMANCE)

if(ANDROID)
  add_compile_definitions(${PHOBOS_DEFINITIONS} SLJIT_CONFIG_ARM_64=1)
  add_compile_options(-w -O3 -flto=thin -ftree-vectorize -funroll-loops -fno-math-errno -fno-trapping-math)
  add_subdirectory(thirdparty/libadrenotools)
else()
  # SDL is configured before the core's definitions and include paths exist, so it builds
  # with its own: the core's VULKAN macro breaks SDL's GPU driver table.
  include(FetchContent)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.30
    GIT_SHALLOW TRUE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  add_compile_definitions(${PHOBOS_DEFINITIONS})
  add_compile_options(-w)
  # sljit picks its target from the compiler (x86-64 or ARM64). On x86-64 the N64 RSP's
  # vector unit uses SSE4.1 when the compiler may emit it, and a scalar path otherwise.
  if(APPLE)
    add_compile_options("SHELL:-Xarch_x86_64 -msse4.1")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    add_compile_options(-msse4.1)
  endif()
  # desktop/compat/android/log.h stands in for the NDK's logging header.
  include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/desktop/compat)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/nall/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/libco
    ${CMAKE_CURRENT_SOURCE_DIR}/ares
    ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp/vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp/util
    ${CMAKE_CURRENT_SOURCE_DIR}/ares/n64/vulkan/parallel-rdp/parallel-rdp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libadrenotools/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vma
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/GL
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/KHR
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/TZXFile
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/jansson
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/miniz-3.1.1
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/zstd-1.5.7
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr/deps/lzma-25.01/src/real
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/librashader/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qon
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit/sljit_src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljitAllocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljitConfigPost.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljitConfigPre.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sse2neon.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xxhash.h
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ymfm
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ymfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xxhash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/GLFW
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Vulkan-Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Vulkan-Headers/include
)

set(ARES_SOURCES
    ares/component/processor/arm7tdmi/arm7tdmi.cpp
    ares/component/processor/armv6m/armv6m.cpp
    ares/component/processor/m68000/m68000.cpp
    ares/component/processor/mos6502/mos6502.cpp
    ares/component/processor/hg51b/hg51b.cpp
    ares/component/processor/sh2/sh2.cpp
    ares/component/processor/z80/z80.cpp
    ares/component/processor/spc700/spc700.cpp
    ares/component/processor/v30mz/v30mz.cpp
    ares/component/processor/sm83/sm83.cpp
    ares/a26/a26.cpp
    ares/cv/cv.cpp
    ares/fc/fc.cpp
    ares/gb/gb.cpp
    ares/gba/gba.cpp
    ares/md/md.cpp
    ares/ms/ms.cpp
    ares/msx/msx.cpp
    ares/n64/n64.cpp
    ares/ng/ng.cpp
    ares/ngp/ngp.cpp
    ares/pce/pce.cpp
    ares/ps1/ps1.cpp
    ares/psp/psp.cpp
    ares/saturn/system/system.cpp
    ares/sfc/sfc.cpp
    ares/sg/sg.cpp
    ares/spec/spec.cpp
    ares/ws/ws.cpp
)

set(OTHER_SOURCES
    nall/nall/nall.cpp
    nall/nall/gdb/server.cpp
    mia/mia.cpp
    libco/libco.c
    thirdparty/volk/volk.c
    thirdparty/GLFW/glfw_stub.cpp
    android/app/src/main/cpp/PhobosRunner.cpp
    ares/ares/android_globals.cpp
    ares/pce/vdp-performance/vdp.cpp
    thirdparty/libchdr/src/libchdr_bitstream.c
    thirdparty/libchdr/src/libchdr_cdrom.c
    thirdparty/libchdr/src/libchdr_chd.c
    thirdparty/libchdr/src/libchdr_flac.c
    thirdparty/libchdr/src/libchdr_huffman.c
    thirdparty/libchdr/src/libchdr_codec_cdfl.c
    thirdparty/libchdr/src/libchdr_codec_cdlz.c
    thirdparty/libchdr/src/libchdr_codec_cdzl.c
    thirdparty/libchdr/src/libchdr_codec_cdzs.c
    thirdparty/libchdr/src/libchdr_codec_flac.c
    thirdparty/libchdr/src/libchdr_codec_huff.c
    thirdparty/libchdr/src/libchdr_codec_lzma.c
    thirdparty/libchdr/src/libchdr_codec_zlib.c
    thirdparty/libchdr/src/libchdr_codec_zstd.c
    thirdparty/libchdr/deps/lzma-25.01/src/real/LzmaDec.c
    thirdparty/libchdr/deps/miniz-3.1.1/miniz.c
    thirdparty/libchdr/deps/zstd-1.5.7/zstddeclib.c
    ares/ares/debug/debug.cpp
    ares/ares/node/node.cpp
    ares/ares/memory/fixed-allocator.cpp
    ares/component/audio/ay38910/ay38910.cpp
    ares/component/audio/msm5205/msm5205.cpp
    ares/component/audio/sn76489/sn76489.cpp
    ares/component/audio/t6w28/t6w28.cpp
    ares/component/audio/ym2149/ym2149.cpp
    ares/component/audio/ym2413/ym2413.cpp
    ares/component/audio/ym2612/ym2612.cpp
    ares/component/eeprom/m24c/m24c.cpp
    ares/component/eeprom/m93lcx6/m93lcx6.cpp
    ares/component/flash/sst39sf0x0/sst39sf0x0.cpp
    ares/component/io/i8255/i8255.cpp
    ares/component/processor/gsu/gsu.cpp
    ares/component/processor/huc6280/huc6280.cpp
    ares/component/processor/i8080/i8080.cpp
    ares/component/processor/m68hc05/m68hc05.cpp
    ares/component/processor/sm5k/sm5k.cpp
    ares/component/processor/ssp1601/ssp1601.cpp
    ares/component/processor/tlcs900h/tlcs900h.cpp
    ares/component/processor/upd96050/upd96050.cpp
    ares/component/processor/wdc65816/wdc65816.cpp
    ares/component/rtc/s3511a/s3511a.cpp
    ares/component/video/tms9918/tms9918.cpp
    ares/component/video/v9938/v9938.cpp
    ares/pce/vdp-performance/vdp.cpp
    ares/n64/vulkan/vulkan.cpp
    thirdparty/sljit/sljit_src/sljitLir.c
    thirdparty/qon/qon.cpp
    thirdparty/sljitAllocator.cpp
    thirdparty/ymfm/src/ymfm_adpcm.cpp
    thirdparty/ymfm/src/ymfm_misc.cpp
    thirdparty/ymfm/src/ymfm_opl.cpp
    thirdparty/ymfm/src/ymfm_opm.cpp
    thirdparty/ymfm/src/ymfm_opn.cpp
    thirdparty/ymfm/src/ymfm_opq.cpp
    thirdparty/ymfm/src/ymfm_opz.cpp
    thirdparty/ymfm/src/ymfm_pcm.cpp
    thirdparty/ymfm/src/ymfm_ssg.cpp
    thirdparty/TZXFile/TZXAudioGenerator.cpp
    thirdparty/TZXFile/TZXBlock.cpp
    thirdparty/TZXFile/TZXBlockArchiveInfo.cpp
    thirdparty/TZXFile/TZXBlockCustomInfo.cpp
    thirdparty/TZXFile/TZXBlockGroupEnd.cpp
    thirdparty/TZXFile/TZXBlockGroupStart.cpp
    thirdparty/TZXFile/TZXBlockHardwareType.cpp
    thirdparty/TZXFile/TZXBlockKansasCityStandard.cpp
    thirdparty/TZXFile/TZXBlockLoopEnd.cpp
    thirdparty/TZXFile/TZXBlockLoopStart.cpp
    thirdparty/TZXFile/TZXBlockMessage.cpp
    thirdparty/TZXFile/TZXBlockPause.cpp
    thirdparty/TZXFile/TZXBlockPulseSequence.cpp
    thirdparty/TZXFile/TZXBlockPureData.cpp
    thirdparty/TZXFile/TZXBlockPureTone.cpp
    thirdparty/TZXFile/TZXBlockStandardSpeedData.cpp
    thirdparty/TZXFile/TZXBlockStopTheTape48K.cpp
    thirdparty/TZXFile/TZXBlockTextDescription.cpp
    thirdparty/TZXFile/TZXBlockTurboSpeedData.cpp
    thirdparty/TZXFile/TZXFile.cpp
    ares/n64/vulkan/parallel-rdp/parallel-rdp/command_ring.cpp
    ares/n64/vulkan/parallel-rdp/parallel-rdp/rdp_device.cpp
    ares/n64/vulkan/parallel-rdp/parallel-rdp/rdp_dump_write.cpp
    ares/n64/vulkan/parallel-rdp/parallel-rdp/rdp_renderer.cpp
    ares/n64/vulkan/parallel-rdp/parallel-rdp/video_interface.cpp
    ares/n64/vulkan/parallel-rdp/util/aligned_alloc.cpp
    ares/n64/vulkan/parallel-rdp/util/arena_allocator.cpp
    ares/n64/vulkan/parallel-rdp/util/environment.cpp
    ares/n64/vulkan/parallel-rdp/util/logging.cpp
    ares/n64/vulkan/parallel-rdp/util/thread_id.cpp
    ares/n64/vulkan/parallel-rdp/util/thread_name.cpp
    ares/n64/vulkan/parallel-rdp/util/timeline_trace_file.cpp
    ares/n64/vulkan/parallel-rdp/util/timer.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/buffer.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/buffer_pool.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/command_buffer.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/command_pool.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/context.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/cookie.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/descriptor_set.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/device.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/event_manager.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/fence.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/fence_manager.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/image.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/indirect_layout.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/memory_allocator.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/pipeline_event.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/query_pool.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/render_pass.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/sampler.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/semaphore.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/semaphore_manager.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/shader.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/wsi.cpp
    ares/n64/vulkan/parallel-rdp/vulkan/texture/texture_format.cpp
)

# The firmware ares bundles (mia/resource/resource.bml, data in mia/Firmware), shipped as upstream
# ships it: upstream writes these arrays with its sourcery tool, this at configure time.
set(MIA_RESOURCE_BML ${CMAKE_CURRENT_SOURCE_DIR}/mia/resource/resource.bml)
set(MIA_RESOURCE_CPP ${CMAKE_CURRENT_BINARY_DIR}/mia-resource.cpp)
file(STRINGS "${MIA_RESOURCE_BML}" MIA_RESOURCE_LINES)
set(MIA_RESOURCE_INPUTS "${MIA_RESOURCE_BML}")
set(MIA_RESOURCE_CODE "// Generated from mia/resource/resource.bml by CMakeLists.txt.\n#include <mia/resource/resource.hpp>\n\nnamespace mia::Resource {\n")
set(MIA_RESOURCE_NAMESPACE "")
foreach(line IN LISTS MIA_RESOURCE_LINES)
  if(line MATCHES "^  namespace name=([A-Za-z0-9]+)")
    if(MIA_RESOURCE_NAMESPACE)
      string(APPEND MIA_RESOURCE_CODE "}\n")
    endif()
    set(MIA_RESOURCE_NAMESPACE ${CMAKE_MATCH_1})
    string(APPEND MIA_RESOURCE_CODE "namespace ${MIA_RESOURCE_NAMESPACE} {\n")
  elseif(line MATCHES "binary name=([A-Za-z0-9]+) file=\"([^\"]+)\"")
    set(MIA_RESOURCE_NAME ${CMAKE_MATCH_1})
    get_filename_component(MIA_RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/mia/resource/${CMAKE_MATCH_2}" ABSOLUTE)
    list(APPEND MIA_RESOURCE_INPUTS "${MIA_RESOURCE_FILE}")
    file(READ "${MIA_RESOURCE_FILE}" MIA_RESOURCE_HEX HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," MIA_RESOURCE_BYTES "${MIA_RESOURCE_HEX}")
    string(APPEND MIA_RESOURCE_CODE
      "static const uint8_t ${MIA_RESOURCE_NAME}Data[] = {${MIA_RESOURCE_BYTES}};\n"
      "const Blob ${MIA_RESOURCE_NAME}{${MIA_RESOURCE_NAME}Data, sizeof ${MIA_RESOURCE_NAME}Data};\n")
  endif()
endforeach()
string(APPEND MIA_RESOURCE_CODE "}\n}\n")
file(CONFIGURE OUTPUT "${MIA_RESOURCE_CPP}" CONTENT "${MIA_RESOURCE_CODE}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MIA_RESOURCE_INPUTS})

add_library(phobos_core STATIC
    ${ARES_SOURCES}
    ${OTHER_SOURCES}
    ${MIA_RESOURCE_CPP}
)

set(ARES_ALL_SOURCES
    ${ARES_SOURCES}
    ares/ares/android_globals.cpp
    ares/ares/debug/debug.cpp
    ares/ares/node/node.cpp
    ares/ares/memory/fixed-allocator.cpp
    ares/component/audio/ay38910/ay38910.cpp
    ares/component/audio/msm5205/msm5205.cpp
    ares/component/audio/sn76489/sn76489.cpp
    ares/component/audio/t6w28/t6w28.cpp
    ares/component/audio/ym2149/ym2149.cpp
    ares/component/audio/ym2413/ym2413.cpp
    ares/component/audio/ym2612/ym2612.cpp
    ares/component/eeprom/m24c/m24c.cpp
    ares/component/eeprom/m93lcx6/m93lcx6.cpp
    ares/component/flash/sst39sf0x0/sst39sf0x0.cpp
    ares/component/io/i8255/i8255.cpp
    ares/component/processor/gsu/gsu.cpp
    ares/component/processor/huc6280/huc6280.cpp
    ares/component/processor/i8080/i8080.cpp
    ares/component/processor/m68hc05/m68hc05.cpp
    ares/component/processor/sm5k/sm5k.cpp
    ares/component/processor/ssp1601/ssp1601.cpp
    ares/component/processor/tlcs900h/tlcs900h.cpp
    ares/component/processor/upd96050/upd96050.cpp
    ares/component/processor/wdc65816/wdc65816.cpp
    ares/component/rtc/s3511a/s3511a.cpp
    ares/component/video/tms9918/tms9918.cpp
    ares/component/video/v9938/v9938.cpp
    ares/pce/vdp-performance/vdp.cpp
    ares/n64/vulkan/vulkan.cpp
    nall/nall/gdb/server.cpp
)

set_property(SOURCE ${ARES_ALL_SOURCES} PROPERTY COMPILE_OPTIONS "-include" "${CMAKE_CURRENT_SOURCE_DIR}/ares/ares/ares.hpp")

# FFmpeg's LGPL decoders for the PSP's music and movies (docs/psp-core.md, parts 26 and 27): built from the official
# release by thirdparty/ffmpeg/build.sh (once, then kept in its cache) and linked as the shared libraries libavcodec and
# libavutil, which each package carries beside Phobos, as the LGPL asks. -DPHOBOS_FFMPEG=OFF builds without them: the
# PSP then has no music or movies, as before.
option(PHOBOS_FFMPEG "Decode the PSP's music and movies with FFmpeg's LGPL decoders" ON)
set(FFMPEG_BUILD ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ffmpeg/build.sh)

if(ANDROID)
  add_library(phobos_android SHARED
      android/app/src/main/cpp/PhobosJNI.cpp
      android/app/src/main/cpp/PhobosHostAndroid.cpp
  )

  find_library(log-lib log)
  find_library(android-lib android)
  find_library(aaudio-lib aaudio)
  find_library(vulkan-lib vulkan)
  find_library(egl-lib EGL)
  find_library(gles-lib GLESv3)
  find_library(z-lib z)
  find_library(dl-lib dl)

  target_link_libraries(phobos_android PRIVATE phobos_core ${log-lib} ${android-lib} ${aaudio-lib} ${vulkan-lib} ${egl-lib} ${gles-lib} ${z-lib} ${dl-lib} adrenotools)

  # FFmpeg (above), for arm64-v8a with the NDK: libavcodec.so and libavutil.so, which the APK carries beside Phobos's
  # own library.
  if(PHOBOS_FFMPEG)
    set(FFMPEG_NDK ${CMAKE_ANDROID_NDK})
    if(NOT FFMPEG_NDK)
      set(FFMPEG_NDK ${ANDROID_NDK})
    endif()
    execute_process(
      COMMAND bash ${FFMPEG_BUILD} android ${FFMPEG_NDK} ${ANDROID_PLATFORM_LEVEL}
      OUTPUT_VARIABLE FFMPEG_OUTPUT
      RESULT_VARIABLE FFMPEG_RESULT
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT FFMPEG_RESULT EQUAL 0)
      message(FATAL_ERROR "FFmpeg didn't build (above); -DPHOBOS_FFMPEG=OFF builds Phobos without it")
    endif()
    string(REGEX REPLACE ".*\n" "" FFMPEG_DIR "${FFMPEG_OUTPUT}")
    foreach(FFMPEG_LIBRARY avcodec avutil)
      add_library(${FFMPEG_LIBRARY} SHARED IMPORTED)
      set_target_properties(${FFMPEG_LIBRARY} PROPERTIES IMPORTED_LOCATION ${FFMPEG_DIR}/lib/lib${FFMPEG_LIBRARY}.so)
    endforeach()
    # Built again should the cache have gone since CMake ran (a no-op otherwise).
    add_custom_target(ffmpeg COMMAND bash ${FFMPEG_BUILD} android ${FFMPEG_NDK} ${ANDROID_PLATFORM_LEVEL} > /dev/null
      BYPRODUCTS ${FFMPEG_DIR}/lib/libavcodec.so ${FFMPEG_DIR}/lib/libavutil.so)
    add_dependencies(phobos_core ffmpeg)
    target_include_directories(phobos_core SYSTEM PRIVATE ${FFMPEG_DIR}/include)
    target_compile_definitions(phobos_core PRIVATE ARES_ENABLE_FFMPEG)
    target_link_libraries(phobos_android PRIVATE avcodec avutil)
  endif()

  # Objects are ThinLTO bitcode (-flto=thin above). Without LTO flags on the link
  # line lld still runs ThinLTO, but its backend defaults to -O2.
  target_link_options(phobos_android PRIVATE -flto=thin -O3)

  # Only the JNIEXPORT entry points need to be visible outside the library.
  # Hidden symbols let internal calls and global accesses skip the PLT and GOT.
  # Not global: libadrenotools' hook libraries export their entry points by default.
  target_compile_options(phobos_core PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
  target_compile_options(phobos_android PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
else()
  # Cores call __android_log_print without including <android/log.h>.
  target_compile_options(phobos_core PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/desktop/compat/android/log.h>")

  add_executable(phobos
      desktop/main.cpp
      desktop/PhobosHostDesktop.cpp
      desktop/Firmware.cpp
      desktop/Input.cpp
      desktop/Library.cpp
      desktop/PspDiscInfo.cpp
      desktop/Settings.cpp
  )
  find_package(Threads REQUIRED)
  target_link_libraries(phobos PRIVATE phobos_core SDL3::SDL3-static Threads::Threads ${CMAKE_DL_LIBS})
  # The boot ROMs and game databases the Android app ships as assets, beside the program; it
  # copies them into its data folder at start, as the app's extractAssets() does.
  add_custom_command(TARGET phobos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/assets/Database $<TARGET_FILE_DIR:phobos>/Database
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/assets/System $<TARGET_FILE_DIR:phobos>/System)
  if(WIN32)
    # The icon is made from the PNG at configure time when Python is around; without it the
    # executable just has no icon.
    set(PHOBOS_ICON_RESOURCE "")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
      execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/desktop/windows/make-ico.py
                ${CMAKE_CURRENT_SOURCE_DIR}/ares/ares/resource/icon@2x.png ${CMAKE_CURRENT_BINARY_DIR}/phobos.ico
        RESULT_VARIABLE PHOBOS_ICON_RESULT)
      if(PHOBOS_ICON_RESULT EQUAL 0)
        set(PHOBOS_ICON_RESOURCE "1 ICON \"${CMAKE_CURRENT_BINARY_DIR}/phobos.ico\"")
      endif()
    endif()
    configure_file(desktop/windows/phobos.rc.in ${CMAKE_CURRENT_BINARY_DIR}/phobos.rc @ONLY)
    # Phobos.exe needs nothing beside it but FFmpeg's two DLLs (below): the C++ runtime and
    # winpthreads link in statically, and Vulkan is loaded at run time from the driver's vulkan-1.dll.
    target_sources(phobos PRIVATE desktop/PlatformWindows.cpp ${CMAKE_CURRENT_BINARY_DIR}/phobos.rc)
    target_link_options(phobos PRIVATE -static $<$<CONFIG:Release>:-s>)
    # nall's registry helpers use shlwapi; its gdb server, Winsock.
    target_link_libraries(phobos PRIVATE shlwapi ws2_32)
    set_target_properties(phobos PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME Phobos)
  endif()

  # FFmpeg (above), for the desktop: the host's own build (Linux; Windows under MSYS2), each of the Mac's architectures
  # built and combined into universal libraries, or Windows's from Linux with MinGW-w64. The libraries are copied
  # beside the program, under the names it loads them by (libavcodec.so.63, libavcodec.63.dylib, avcodec-63.dll), and
  # it looks for them there (its run path is its own folder; Windows looks there first); the packages take them from
  # there (scripts/package-*.sh).
  if(PHOBOS_FFMPEG)
    set(FFMPEG_ENVIRONMENT "")
    if(APPLE)
      set(FFMPEG_TARGET macos ${CMAKE_OSX_ARCHITECTURES})
      if(NOT CMAKE_OSX_ARCHITECTURES)
        set(FFMPEG_TARGET macos ${CMAKE_SYSTEM_PROCESSOR})
      endif()
      if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(FFMPEG_ENVIRONMENT MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
      endif()
    elseif(WIN32 AND CMAKE_CROSSCOMPILING)
      # MinGW-w64's tools by the C compiler's name: x86_64-w64-mingw32-gcc-posix's are x86_64-w64-mingw32-.
      get_filename_component(FFMPEG_COMPILER ${CMAKE_C_COMPILER} NAME)
      string(REGEX REPLACE "gcc(-posix|-win32)?(\\.exe)?$" "" FFMPEG_TOOLS ${FFMPEG_COMPILER})
      set(FFMPEG_TARGET windows ${FFMPEG_TOOLS})
    elseif(CMAKE_CROSSCOMPILING)
      message(FATAL_ERROR "thirdparty/ffmpeg/build.sh can't build FFmpeg for this target; -DPHOBOS_FFMPEG=OFF builds Phobos without it")
    else()
      set(FFMPEG_TARGET host)
    endif()
    set(FFMPEG_COMMAND ${CMAKE_COMMAND} -E env ${FFMPEG_ENVIRONMENT} bash ${FFMPEG_BUILD} ${FFMPEG_TARGET})
    execute_process(
      COMMAND ${FFMPEG_COMMAND}
      OUTPUT_VARIABLE FFMPEG_OUTPUT
      RESULT_VARIABLE FFMPEG_RESULT
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT FFMPEG_RESULT EQUAL 0)
      message(FATAL_ERROR "FFmpeg didn't build (above); -DPHOBOS_FFMPEG=OFF builds Phobos without it")
    endif()
    string(REGEX REPLACE ".*\n" "" FFMPEG_DIR "${FFMPEG_OUTPUT}")
    # Each library: the file the program links (the shared library, or Windows's import library) and the one it loads.
    set(FFMPEG_FILES "")
    foreach(FFMPEG_LIBRARY avcodec avutil)
      if(WIN32)
        set(FFMPEG_LINKED ${FFMPEG_DIR}/lib/lib${FFMPEG_LIBRARY}.dll.a)
        file(GLOB FFMPEG_LOADED ${FFMPEG_DIR}/bin/${FFMPEG_LIBRARY}-*.dll)
      elseif(APPLE)
        set(FFMPEG_LINKED ${FFMPEG_DIR}/lib/lib${FFMPEG_LIBRARY}.dylib)
        file(GLOB FFMPEG_LOADED ${FFMPEG_DIR}/lib/lib${FFMPEG_LIBRARY}.*.dylib)
        list(FILTER FFMPEG_LOADED INCLUDE REGEX "/lib${FFMPEG_LIBRARY}\\.[0-9]+\\.dylib$")
      else()
        set(FFMPEG_LINKED ${FFMPEG_DIR}/lib/lib${FFMPEG_LIBRARY}.so)
        file(GLOB FFMPEG_LOADED ${FFMPEG_DIR}/lib/lib${FFMPEG_LIBRARY}.so.*)
        list(FILTER FFMPEG_LOADED INCLUDE REGEX "/lib${FFMPEG_LIBRARY}\\.so\\.[0-9]+$")
      endif()
      list(LENGTH FFMPEG_LOADED FFMPEG_COUNT)
      if(NOT FFMPEG_COUNT EQUAL 1)
        message(FATAL_ERROR "FFmpeg: ${FFMPEG_DIR} doesn't hold one lib${FFMPEG_LIBRARY} to load (${FFMPEG_LOADED})")
      endif()
      add_library(${FFMPEG_LIBRARY} SHARED IMPORTED)
      if(WIN32)
        set_target_properties(${FFMPEG_LIBRARY} PROPERTIES IMPORTED_IMPLIB ${FFMPEG_LINKED} IMPORTED_LOCATION ${FFMPEG_LOADED})
      else()
        set_target_properties(${FFMPEG_LIBRARY} PROPERTIES IMPORTED_LOCATION ${FFMPEG_LINKED})
      endif()
      # (copied from the file the name leads to: on Linux and macOS it's a link to the library's full version)
      get_filename_component(FFMPEG_NAME ${FFMPEG_LOADED} NAME)
      get_filename_component(FFMPEG_REAL ${FFMPEG_LOADED} REALPATH)
      add_custom_command(TARGET phobos POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${FFMPEG_REAL} $<TARGET_FILE_DIR:phobos>/${FFMPEG_NAME})
      list(APPEND FFMPEG_FILES ${FFMPEG_LINKED} ${FFMPEG_LOADED})
    endforeach()
    # Built again should the cache have gone since CMake ran (a no-op otherwise).
    add_custom_target(ffmpeg COMMAND ${FFMPEG_COMMAND} BYPRODUCTS ${FFMPEG_FILES} VERBATIM)
    add_dependencies(phobos_core ffmpeg)
    target_include_directories(phobos_core SYSTEM PRIVATE ${FFMPEG_DIR}/include)
    target_compile_definitions(phobos_core PRIVATE ARES_ENABLE_FFMPEG)
    target_link_libraries(phobos PRIVATE avcodec avutil)
    if(APPLE)
      set_target_properties(phobos PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path")
    elseif(NOT WIN32)
      set_target_properties(phobos PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    endif()
  endif()
endif()
