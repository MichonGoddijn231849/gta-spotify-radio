cmake_minimum_required(VERSION 3.25)
project(GtaSpotifyRadio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

file(GLOB_RECURSE LIBRESPOT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/librespotclib/src/*.cpp")

add_library(librespotc STATIC
    ${LIBRESPOT_SOURCES}
    vendor/librespotclib/deps/stb/stb_vorbis.c
)
target_include_directories(librespotc PUBLIC
    vendor/librespotclib/include
    vendor/librespotclib/deps
)
target_compile_definitions(librespotc PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(librespotc PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    $<$<COMPILE_LANGUAGE:C>:/wd4244 /wd4245 /wd4456 /wd4457 /wd4701 /wd4702>
)
target_link_libraries(librespotc PUBLIC bcrypt winhttp winmm ws2_32)

add_library(GtaSpotifyRadio SHARED
    src/game_audio.cpp
    src/log.cpp
    src/main.cpp
    src/settings.cpp
    src/silent_track.cpp
    src/spotify_radio.cpp
    src/station_ownership.cpp
    src/xaudio_player.cpp
)
set_target_properties(GtaSpotifyRadio PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "GtaSpotifyRadio"
)
target_include_directories(GtaSpotifyRadio PRIVATE
    src
    vendor/librespotclib/include
    vendor/scripthookv-template/extern/scripthookv/inc
)
target_compile_definitions(GtaSpotifyRadio PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(GtaSpotifyRadio PRIVATE /EHsc /W4)
target_link_libraries(GtaSpotifyRadio PRIVATE
    librespotc
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/scripthookv-template/extern/scripthookv/lib/ScriptHookV.lib"
    xaudio2
    ole32
    shell32
)

add_executable(SpotifyReceiverTest
    vendor/librespotclib/harness/main.cpp
)
target_link_libraries(SpotifyReceiverTest PRIVATE librespotc)
target_compile_options(SpotifyReceiverTest PRIVATE /EHsc /W4)
