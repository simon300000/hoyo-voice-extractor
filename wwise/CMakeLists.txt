cmake_minimum_required(VERSION 3.20)

project(hoyo_audio_convert LANGUAGES C CXX)

set(VGMSTREAM_DIR "${CMAKE_CURRENT_LIST_DIR}/source/vgmstream")
set(OGG_DIR "${CMAKE_CURRENT_LIST_DIR}/source/ogg")
set(VORBIS_DIR "${CMAKE_CURRENT_LIST_DIR}/source/vorbis")

# Only the decoder portions of the vendored libraries are needed. Keeping the
# targets here makes the source snapshot independent from the upstream build,
# packaging, test, plugin, and dependency-download machinery.
set(INCLUDE_INTTYPES_H 1)
set(INCLUDE_STDINT_H 1)
set(INCLUDE_SYS_TYPES_H 1)
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)
configure_file(
  "${OGG_DIR}/include/ogg/config_types.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/ogg/config_types.h"
  @ONLY
)

add_library(ogg STATIC
  "${OGG_DIR}/src/bitwise.c"
)
target_include_directories(ogg PUBLIC
  "${OGG_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/include"
)

set(VORBIS_SOURCES
  bitrate.c
  block.c
  codebook.c
  envelope.c
  floor0.c
  floor1.c
  info.c
  lpc.c
  lsp.c
  mapping0.c
  mdct.c
  psy.c
  registry.c
  res0.c
  sharedbook.c
  smallft.c
  synthesis.c
  window.c
)
list(TRANSFORM VORBIS_SOURCES PREPEND "${VORBIS_DIR}/lib/")

add_library(vorbis STATIC ${VORBIS_SOURCES})
target_include_directories(vorbis
  PUBLIC "${VORBIS_DIR}/include"
  PRIVATE "${VORBIS_DIR}/lib"
)
target_link_libraries(vorbis PUBLIC ogg)

file(GLOB VGMSTREAM_ROOT_SOURCES CONFIGURE_DEPENDS "${VGMSTREAM_DIR}/src/*.c")
set(VGMSTREAM_BASE_SOURCES
  api_decode_base.c
  api_decode_open.c
  api_decode_play.c
  api_helpers.c
  api_libsf.c
  codec_info.c
  decode.c
  info.c
  mixer.c
  mixer_ops_common.c
  mixer_ops_fade.c
  mixing.c
  mixing_commands.c
  mixing_macros.c
  play_config.c
  play_state.c
  plugins.c
  render.c
  resampler.c
  sbuf.c
  seek.c
  seek_table.c
  streamfile_api.c
  streamfile_clamp.c
  streamfile_fakename.c
  streamfile_stdio.c
  streamfile_wrap.c
)
list(TRANSFORM VGMSTREAM_BASE_SOURCES PREPEND "${VGMSTREAM_DIR}/src/base/")
set(VGMSTREAM_UTIL_SOURCES
  chunks.c
  log.c
  paths.c
  reader_put.c
  sf_utils.c
  vorbis_codebooks.c
)
list(TRANSFORM VGMSTREAM_UTIL_SOURCES PREPEND "${VGMSTREAM_DIR}/src/util/")
set(VGMSTREAM_CODING_SOURCES
  "${VGMSTREAM_DIR}/src/coding/coding_utils.c"
  "${VGMSTREAM_DIR}/src/coding/ima_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/ngc_dsp_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/pcm_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/psx_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/psv_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/ptadpcm_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/vorbis_custom_decoder.c"
  "${VGMSTREAM_DIR}/src/coding/vorbis_custom_utils.c"
  "${VGMSTREAM_DIR}/src/coding/vorbis_custom_utils_wwise.c"
)
set(VGMSTREAM_LAYOUT_SOURCES
  "${VGMSTREAM_DIR}/src/layout/flat.c"
  "${VGMSTREAM_DIR}/src/layout/interleave.c"
)

add_library(libvgmstream STATIC
  ${VGMSTREAM_BASE_SOURCES}
  ${VGMSTREAM_CODING_SOURCES}
  ${VGMSTREAM_LAYOUT_SOURCES}
  ${VGMSTREAM_ROOT_SOURCES}
  ${VGMSTREAM_UTIL_SOURCES}
  "${VGMSTREAM_DIR}/src/meta/wwise.c"
)
set_target_properties(libvgmstream PROPERTIES PREFIX "")
target_compile_definitions(libvgmstream PRIVATE
  HOYO_WWISE_ONLY
  VGM_LOG_OUTPUT
  VGM_USE_VORBIS
)
target_include_directories(libvgmstream PRIVATE
  "${VGMSTREAM_DIR}/ext_includes"
  "${VGMSTREAM_DIR}/src"
  "${VGMSTREAM_DIR}/src/base"
  "${VGMSTREAM_DIR}/src/coding"
  "${VGMSTREAM_DIR}/src/layout"
  "${VGMSTREAM_DIR}/src/meta"
  "${VGMSTREAM_DIR}/src/util"
)
target_link_libraries(libvgmstream PUBLIC vorbis ogg)

add_executable(hoyo-audio-convert
  src/main.cpp
  "${VGMSTREAM_DIR}/cli/wav_utils.c"
)
target_compile_features(hoyo-audio-convert PRIVATE cxx_std_17)
target_include_directories(hoyo-audio-convert PRIVATE
  "${VGMSTREAM_DIR}/src"
  "${VGMSTREAM_DIR}/cli"
)
target_link_libraries(hoyo-audio-convert PRIVATE libvgmstream)

if(NOT WIN32)
  target_link_libraries(hoyo-audio-convert PRIVATE m)
endif()

if(MSVC)
  target_compile_options(hoyo-audio-convert PRIVATE /W4 /O2)
else()
  target_compile_options(hoyo-audio-convert PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

install(TARGETS hoyo-audio-convert RUNTIME DESTINATION bin)
