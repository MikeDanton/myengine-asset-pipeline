cmake_minimum_required(VERSION 3.16)
project(myengine_asset_pipeline VERSION 1.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to enable strict warnings-as-errors for supported compilers.
option(ENABLE_WERROR "Treat warnings as errors where supported" ON)

# Apply stricter warning flags globally for GCC/Clang and MSVC when enabled.
if(ENABLE_WERROR)
  if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)")
    # Include conversion/sign-conversion warnings for stricter checks.
    # Apply only for C++ compilation so third-party C code isn't affected.
    add_compile_options(
      $<$<COMPILE_LANGUAGE:CXX>:-Wall>
      $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
      $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
      $<$<COMPILE_LANGUAGE:CXX>:-Werror>
      $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
      $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
    )
  elseif(MSVC)
    # /W4 is very strict; /WX turns warnings into errors on MSVC
    add_compile_options(
      $<$<COMPILE_LANGUAGE:CXX>:/W4>
      $<$<COMPILE_LANGUAGE:CXX>:/WX>
    )
  endif()
endif()

# Optional clang-tidy integration. Disabled by default because clang-tidy can be
# noisy and slow; enable with -DENABLE_CLANG_TIDY=ON when you want to run it.
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis during build" OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}; enabling analysis")
    # Use a conservative set of checks that are helpful for hardening.
    # Restrict diagnostics to project headers/sources via header-filter so
    # third-party warnings don't drown out useful diagnostics when clang-tidy
    # runs as part of the build. Users can still override CMAKE_CXX_CLANG_TIDY
    # manually if desired.
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-checks=clang-analyzer-*,cppcoreguidelines-*,modernize-*,performance-*,readability-*;-header-filter=^(include|src|tools|tests)/")
  else()
    message(WARNING "ENABLE_CLANG_TIDY requested but clang-tidy was not found on PATH")
  endif()
endif()

option(BUILD_TESTS "Build tests" ON)

include(FetchContent)

# Fetch xxHash (XXH64) for stable 64-bit asset ids
FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG        v0.8.1
)
FetchContent_MakeAvailable(xxhash)

# Fetch BLAKE3 C implementation for 128-bit content hashes
FetchContent_Declare(
  blake3
  GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
)
FetchContent_MakeAvailable(blake3)

# Optionally fetch zstd so builds are hermetic when desired. This mirrors
# how xxHash and blake3 are fetched above and ensures `zstd` target is
# available for linking when the system package is not present.
FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG        v1.5.5
)
FetchContent_MakeAvailable(zstd)

# The BLAKE3 project places its C CMake build under the `c/` subdirectory.
# Add that subdirectory so it creates the `blake3`/`BLAKE3::blake3` target
# we can link against.
if(DEFINED blake3_SOURCE_DIR)
  add_subdirectory(${blake3_SOURCE_DIR}/c ${blake3_BINARY_DIR}/c)
endif()


add_subdirectory(src)
add_subdirectory(tools/assetc)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
