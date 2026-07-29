cmake_minimum_required(VERSION 3.22)
project(phobos)

add_compile_definitions(BUILD_RELEASE VULKAN VK_NO_PROTOTYPES MIA_LIBRARY)
add_compile_options(-w)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/nall/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/libco
    ${CMAKE_CURRENT_SOURCE_DIR}/ares
    ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/volk
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vma
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/GL
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/KHR
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/TZXFile
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/jansson
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libchdr
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/librashader
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/qon
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sljit/sljit_src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ymfm
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ymfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/GLFW
)

set(ARES_SOURCES
    ares/component/processor/arm7tdmi/arm7tdmi.cpp
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
    ares/myvision/myvision.cpp
    ares/n64/n64.cpp
    ares/ng/ng.cpp
    ares/ngp/ngp.cpp
    ares/pce/pce.cpp
    ares/ps1/ps1.cpp
    ares/sfc/sfc.cpp
    ares/sg/sg.cpp
    ares/spec/spec.cpp
    ares/ws/ws.cpp
)

set(OTHER_SOURCES
    nall/nall/nall.cpp
    mia/mia.cpp
    libco/aarch64.c
    thirdparty/volk/volk.c
    thirdparty/GLFW/glfw_stub.cpp
    android/app/src/main/cpp/PhobosRunner.cpp
    android/app/src/main/cpp/PhobosJNI.cpp
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
)

add_library(phobos_android SHARED
    ${ARES_SOURCES}
    ${OTHER_SOURCES}
)

# Apply -include ares/ares.hpp safely mimicking the GNU Makefile
set_property(SOURCE ${ARES_SOURCES} PROPERTY COMPILE_OPTIONS "-include" "ares/ares.hpp")

find_library(log-lib log)
find_library(android-lib android)
find_library(aaudio-lib aaudio)
find_library(vulkan-lib vulkan)
find_library(z-lib z)
find_library(dl-lib dl)

target_link_libraries(phobos_android ${log-lib} ${android-lib} ${aaudio-lib} ${vulkan-lib} ${z-lib} ${dl-lib})
