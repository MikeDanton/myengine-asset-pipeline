cmake_minimum_required(VERSION 3.16)
project(myengine_asset_pipeline VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
