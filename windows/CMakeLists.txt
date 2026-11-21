# WebCam and TV Smart Vision — edizione Windows (Win32 + DirectShow, senza dipendenze esterne)
#   ./fetch-deps.sh
#   cmake -S . -B build-xp  -DCMAKE_TOOLCHAIN_FILE=toolchains/mingw-i686-xp.cmake && cmake --build build-xp
#   cmake -S . -B build-x64 -DCMAKE_TOOLCHAIN_FILE=toolchains/mingw-x86_64.cmake  && cmake --build build-x64
cmake_minimum_required(VERSION 3.16)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" _top REGEX "^project\\(")
string(REGEX MATCH "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${_top}")
project(WebCamTVSmartVisionWin VERSION ${CMAKE_MATCH_1} LANGUAGES C CXX RC)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${SV_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SV_ARCH_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(SRC3 ${TP}/_src)
if(NOT EXISTS ${SRC3}/libogg/src/framing.c)
  message(FATAL_ERROR "Sorgenti delle librerie mancanti: esegui prima ./fetch-deps.sh")
endif()

# ------------------------------------------------------------------ libogg
add_library(ogg STATIC ${SRC3}/libogg/src/bitwise.c ${SRC3}/libogg/src/framing.c)
target_include_directories(ogg PUBLIC ${SRC3}/libogg/include)

# ---------------------------------------------------------------- libvorbis
set(V ${SRC3}/libvorbis/lib)
add_library(vorbis STATIC
  ${V}/analysis.c ${V}/bitrate.c ${V}/block.c ${V}/codebook.c ${V}/envelope.c ${V}/floor0.c ${V}/floor1.c
  ${V}/info.c ${V}/lookup.c ${V}/lpc.c ${V}/lsp.c ${V}/mapping0.c ${V}/mdct.c ${V}/psy.c ${V}/registry.c
  ${V}/res0.c ${V}/sharedbook.c ${V}/smallft.c ${V}/synthesis.c ${V}/window.c ${V}/vorbisenc.c)
target_include_directories(vorbis PUBLIC ${SRC3}/libvorbis/include PRIVATE ${V})
target_link_libraries(vorbis PUBLIC ogg)
target_compile_options(vorbis PRIVATE -w)

# ---------------------------------------------------------------- libtheora
set(T ${SRC3}/libtheora/lib)
set(THEORA_SRC
  ${T}/analyze.c ${T}/apiwrapper.c ${T}/bitpack.c ${T}/decapiwrapper.c ${T}/decinfo.c ${T}/decode.c
  ${T}/dequant.c ${T}/encapiwrapper.c ${T}/encfrag.c ${T}/encinfo.c ${T}/encode.c ${T}/enquant.c
  ${T}/fdct.c ${T}/fragment.c ${T}/huffdec.c ${T}/huffenc.c ${T}/idct.c ${T}/info.c ${T}/internal.c
  ${T}/mathops.c ${T}/mcenc.c ${T}/quant.c ${T}/rate.c ${T}/state.c ${T}/tokenize.c)
# L'assembly x86-64 di libtheora presuppone "long" a 64 bit (ABI Unix): su Win64 produce flussi corrotti,
# quindi lì si usa il codice C portabile. A 32 bit le routine MMX/SSE2 funzionano e vengono attivate.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(SV_THEORA_ASM OFF)
else()
  set(SV_THEORA_ASM ON)
endif()
if(SV_THEORA_ASM)
  list(APPEND THEORA_SRC
    ${T}/x86/mmxencfrag.c ${T}/x86/mmxfdct.c ${T}/x86/mmxfrag.c ${T}/x86/mmxidct.c ${T}/x86/mmxstate.c
    ${T}/x86/sse2encfrag.c ${T}/x86/sse2fdct.c ${T}/x86/sse2idct.c ${T}/x86/x86cpu.c ${T}/x86/x86enc.c
    ${T}/x86/x86enquant.c ${T}/x86/x86state.c)
endif()
add_library(theora STATIC ${THEORA_SRC})
target_include_directories(theora PUBLIC ${SRC3}/libtheora/include PRIVATE ${T})
target_link_libraries(theora PUBLIC ogg)
target_compile_options(theora PRIVATE -w -fomit-frame-pointer)
if(SV_THEORA_ASM)
  target_compile_definitions(theora PRIVATE OC_X86_ASM)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(theora PRIVATE OC_X86_64_ASM)
  endif()
endif()

# ---------------------------------------------------------------- vo-aacenc
set(A ${SRC3}/vo-aacenc)
file(GLOB VOAAC_SRC ${A}/aacenc/src/*.c ${A}/aacenc/basic_op/*.c)
add_library(voaac STATIC ${VOAAC_SRC} ${A}/common/cmnMemory.c)
target_include_directories(voaac PUBLIC ${A}/common/include PRIVATE ${A}/aacenc/inc ${A}/aacenc/basic_op)
target_compile_options(voaac PRIVATE -w)

# --------------------------------------------------- minih264 + minimp4 (C)
add_library(svcodecs STATIC src/minih264_impl.c src/minimp4_impl.c src/stb_impl.c)
target_include_directories(svcodecs PUBLIC ${TP})
target_compile_options(svcodecs PRIVATE -w)

# ------------------------------------------------------------ nucleo comune
add_library(svcore STATIC
  src/common.cpp src/recorder.cpp src/enc_mp4.cpp src/enc_ogg.cpp src/testpattern.cpp)
target_include_directories(svcore PUBLIC src)
target_compile_definitions(svcore PUBLIC UNICODE _UNICODE WINVER=${SV_WINVER} _WIN32_WINNT=${SV_WINVER}
  NOMINMAX SV_VERSION="${PROJECT_VERSION}" SV_TARGET="${SV_TARGET}")
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -DSV_VMAJ=${PROJECT_VERSION_MAJOR} -DSV_VMIN=${PROJECT_VERSION_MINOR} -DSV_VPAT=${PROJECT_VERSION_PATCH}")
target_compile_options(svcore PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(svcore PUBLIC svcodecs voaac theora vorbis ogg)

set(SV_COMMON_LINK -static -static-libgcc -static-libstdc++ -municode ${SV_LINK_FLAGS})
separate_arguments(SV_COMMON_LINK)

# -------------------------------------------------------------- applicazione
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/app.cpp)
  add_executable(WebCamTVSmartVision WIN32
    src/main.cpp src/app.cpp src/app_pages.cpp src/engine.cpp src/capture_dshow.cpp src/audio_io.cpp
    src/imaging.cpp src/settings.cpp src/selftest.cpp src/xp_compat.cpp res/app.rc)
  target_link_libraries(WebCamTVSmartVision PRIVATE svcore
    strmiids ole32 oleaut32 uuid winmm comctl32 shell32 gdi32 user32)
  target_link_options(WebCamTVSmartVision PRIVATE ${SV_COMMON_LINK} -mwindows)
  set_target_properties(WebCamTVSmartVision PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()

# ------------------------------------------------- prova dei codificatori
add_executable(sv-enctest src/enctest.cpp src/xp_compat.cpp)
target_link_libraries(sv-enctest PRIVATE svcore winmm)
target_link_options(sv-enctest PRIVATE ${SV_COMMON_LINK} -mconsole)
