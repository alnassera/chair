cmake_minimum_required(VERSION 3.20)
project(audio-engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- KissFFT (fetched from GitHub, compiled as static lib) ---
include(FetchContent)
FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        131.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(kissfft)

add_library(kissfft STATIC
    ${kissfft_SOURCE_DIR}/kiss_fft.c
    ${kissfft_SOURCE_DIR}/kiss_fftr.c
)
target_include_directories(kissfft PUBLIC
    ${kissfft_SOURCE_DIR}
)
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=float)

# --- Audio Engine ---
add_executable(audio-engine
    src/main.cpp
    src/capture.cpp
    src/analysis.cpp
    src/classifier.cpp
    src/ipc.cpp
    src/clip_logger.cpp
)
target_include_directories(audio-engine PRIVATE src)
target_link_libraries(audio-engine PRIVATE
    kissfft
    ole32
    uuid
    winmm
    ksuser
)

# Static link: single .exe, no MinGW runtime DLLs needed on target machine
target_link_options(audio-engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-static>
)

# Warnings
target_compile_options(audio-engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wno-missing-field-initializers>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
