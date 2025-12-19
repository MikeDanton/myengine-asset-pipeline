add_executable(pak_roundtrip
  pak_roundtrip.cpp
)

target_include_directories(pak_roundtrip PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(pak_roundtrip PRIVATE asset_runtime)

add_test(NAME pak_roundtrip COMMAND pak_roundtrip)

add_executable(pak_roundtrip_full
  pak_roundtrip_full.cpp
)

target_include_directories(pak_roundtrip_full PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(pak_roundtrip_full PRIVATE asset_runtime)
add_test(NAME pak_roundtrip_full COMMAND pak_roundtrip_full)

add_executable(pak_roundtrip_zstd
  pak_roundtrip_zstd.cpp
)

target_include_directories(pak_roundtrip_zstd PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(pak_roundtrip_zstd PRIVATE asset_runtime)

find_package(ZSTD QUIET)
if(ZSTD_FOUND OR TARGET zstd OR TARGET libzstd OR TARGET ZSTD::ZSTD)
  # Only register the test when zstd is available on the system or provided
  # as a target; otherwise the executable still exists so IDEs (CLion)
  # recognize the source file as part of a target but CTest won't run it.
  add_test(NAME pak_roundtrip_zstd COMMAND pak_roundtrip_zstd)
else()
  message(STATUS "ZSTD not found; pak_roundtrip_zstd executable added but test will be skipped")
endif()

# If the blake3 target is available add it to test executables so the
# final link resolves symbols (FetchContent may provide BLAKE3::blake3).
if(TARGET BLAKE3::blake3)
  target_link_libraries(pak_roundtrip_full PRIVATE BLAKE3::blake3)
  if(TARGET pak_roundtrip_zstd)
    target_link_libraries(pak_roundtrip_zstd PRIVATE BLAKE3::blake3)
  endif()
endif()

add_executable(pak_negative_toc_corrupt
  pak_negative_toc_corrupt.cpp
)
target_include_directories(pak_negative_toc_corrupt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(pak_negative_toc_corrupt PRIVATE asset_runtime)
add_test(NAME pak_negative_toc_corrupt COMMAND pak_negative_toc_corrupt)

add_executable(pak_negative_missing_asset
  pak_negative_missing_asset.cpp
)
target_include_directories(pak_negative_missing_asset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(pak_negative_missing_asset PRIVATE asset_runtime)
add_test(NAME pak_negative_missing_asset COMMAND pak_negative_missing_asset)

add_executable(pak_negative_hash_mismatch
  pak_negative_hash_mismatch.cpp
)
target_include_directories(pak_negative_hash_mismatch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(pak_negative_hash_mismatch PRIVATE asset_runtime)
add_test(NAME pak_negative_hash_mismatch COMMAND pak_negative_hash_mismatch)

add_executable(pak_negative_decompress_fail
  pak_negative_decompress_fail.cpp
)
target_include_directories(pak_negative_decompress_fail PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(pak_negative_decompress_fail PRIVATE asset_runtime)
add_test(NAME pak_negative_decompress_fail COMMAND pak_negative_decompress_fail)

add_executable(pak_hash_regression
  pak_hash_regression.cpp
)
target_include_directories(pak_hash_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(pak_hash_regression PRIVATE asset_runtime)
add_test(NAME pak_hash_regression COMMAND pak_hash_regression)
