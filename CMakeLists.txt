cmake_minimum_required(VERSION 3.24)
project(myengine_asset_pipeline VERSION 1.1.0 LANGUAGES CXX C)

# This repository is consumed by Clang-only MyEngine2. Restrict standalone
# configurations too, with one obvious CMake guard to relax if GCC support
# is validated later. C sources still use the selected C compiler normally.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
  message(FATAL_ERROR
    "myengine-asset-pipeline supports Clang only (detected ${CMAKE_CXX_COMPILER_ID}). "
    "Configure with -DCMAKE_CXX_COMPILER=clang++ in a fresh build directory.")
endif()

option(ENABLE_WERROR "Treat project warnings as errors" ON)
option(ENABLE_CLANG_TIDY "Run clang-tidy on project C++ targets" OFF)
option(ASSET_PIPELINE_BUILD_TESTS "Build asset pipeline tests" ${PROJECT_IS_TOP_LEVEL})
option(ASSET_PIPELINE_WITH_ZSTD "Enable zstd compression when available" ON)
include(GNUInstallDirs)
include(FetchContent)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Populate xxHash without invoking its legacy CMake project. Its C implementation
# is compiled into asset_runtime, so installed users need no separate xxHash target.
FetchContent_Declare(xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.1
  SOURCE_SUBDIR asset_pipeline_no_cmake)
FetchContent_Declare(blake3
  GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
  GIT_TAG 657284b1fa4c4378aa7422b4450dcb5c2c9052fc
  SOURCE_SUBDIR c)
FetchContent_MakeAvailable(xxhash blake3)
# This pinned BLAKE3 revision generates its pkg-config file in its own binary
# directory but installs from the top-level binary directory when embedded.
configure_file("${blake3_BINARY_DIR}/libblake3.pc"
               "${CMAKE_BINARY_DIR}/libblake3.pc" COPYONLY)

set(ASSET_PIPELINE_HAS_ZSTD OFF)
if(ASSET_PIPELINE_WITH_ZSTD)
  find_package(ZSTD QUIET)
  if(ZSTD_FOUND)
    set(ASSET_PIPELINE_HAS_ZSTD ON)
  else()
    message(STATUS "zstd not found: compression disabled (install zstd development files to enable)")
  endif()
endif()

function(asset_pipeline_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>)
    if(ENABLE_WERROR)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
    endif()
  elseif(MSVC)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
    if(ENABLE_WERROR)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/WX>)
    endif()
  endif()
  if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  endif()
endfunction()

add_subdirectory(src)
add_subdirectory(tools/assetc)
if(ASSET_PIPELINE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
