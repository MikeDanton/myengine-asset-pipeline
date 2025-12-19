add_library(asset_runtime STATIC
  pak_reader.cpp
  pak_writer.cpp
  hash.cpp
  xxhash64.cpp
  hash128.cpp
  compression_zstd.cpp
)

target_include_directories(asset_runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(asset_runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

# Install/export rules so downstream projects can find this library
include(CMakePackageConfigHelpers)
set(CMAKE_INSTALL_INCLUDEDIR include)
set(CMAKE_INSTALL_LIBDIR lib)

install(TARGETS asset_runtime
  EXPORT asset_pipelineTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT asset_pipelineTargets
  FILE asset_pipelineTargets.cmake
  NAMESPACE AssetPipeline::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asset_pipeline
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/AssetPipelineConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asset_pipeline
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asset_pipeline)

# Try to find ZSTD library for compression support. Prefer a system
# package, but fall back to any vendored FetchContent target that may
# provide zstd (e.g. `zstd` / `libzstd` / `ZSTD::ZSTD`).
find_package(ZSTD QUIET)
if(ZSTD_FOUND)
  target_link_libraries(asset_runtime PRIVATE ZSTD::ZSTD)
  target_compile_definitions(asset_runtime PRIVATE ASSET_HAS_ZSTD=1)
else()
  if(TARGET ZSTD::ZSTD)
    target_link_libraries(asset_runtime PRIVATE ZSTD::ZSTD)
    target_compile_definitions(asset_runtime PRIVATE ASSET_HAS_ZSTD=1)
  elseif(TARGET libzstd)
    target_link_libraries(asset_runtime PRIVATE libzstd)
    target_compile_definitions(asset_runtime PRIVATE ASSET_HAS_ZSTD=1)
  elseif(TARGET zstd)
    target_link_libraries(asset_runtime PRIVATE zstd)
    target_compile_definitions(asset_runtime PRIVATE ASSET_HAS_ZSTD=1)
  else()
    target_compile_definitions(asset_runtime PRIVATE ASSET_HAS_ZSTD=0)
  endif()
endif()

# Link xxhash (provided via FetchContent in top-level CMake). For blake3
# we compile the single-file C implementation into our static library so
# consumers don't need to link an external blake3 library.

# Provide include paths from the FetchContent source directories so headers
# like <xxhash.h> and <blake3.h> are findable when the upstream projects
# don't export an imported target with proper include dirs.
if(DEFINED xxhash_SOURCE_DIR)
  target_include_directories(asset_runtime PRIVATE ${xxhash_SOURCE_DIR})
  # Compile the xxHash C implementation into our static library so XXH64 is
  # available at link time without requiring an external lib target.
  if(EXISTS "${xxhash_SOURCE_DIR}/xxhash.c")
    target_sources(asset_runtime PRIVATE ${xxhash_SOURCE_DIR}/xxhash.c)
  endif()
endif()
if(DEFINED blake3_SOURCE_DIR)
  target_include_directories(asset_runtime PRIVATE ${blake3_SOURCE_DIR}/c)
  # Link against the blake3 target provided by the upstream CMake (alias
  # `BLAKE3::blake3`) so we get the full, optimized implementation with
  # optional SIMD sources.
  if(TARGET BLAKE3::blake3)
    target_link_libraries(asset_runtime PRIVATE BLAKE3::blake3)
  else()
    # Fallback: if the alias isn't present, try the plain 'blake3' target
    target_link_libraries(asset_runtime PRIVATE blake3)
  endif()
endif()
