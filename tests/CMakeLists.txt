set(asset_pipeline_tests
  pak_roundtrip pak_roundtrip_full pak_loadto pak_negative_toc_corrupt
  pak_negative_missing_asset pak_negative_hash_mismatch pak_negative_decompress_fail
  pak_hash_regression pak_safety)
if(ASSET_PIPELINE_HAS_ZSTD)
  list(APPEND asset_pipeline_tests pak_roundtrip_zstd)
endif()
foreach(test IN LISTS asset_pipeline_tests)
  add_executable(${test} ${test}.cpp)
  asset_pipeline_configure_target(${test})
  target_link_libraries(${test} PRIVATE AssetPipeline::asset_runtime)
  target_compile_definitions(${test} PRIVATE ASSET_HAS_ZSTD=$<BOOL:${ASSET_PIPELINE_HAS_ZSTD}>)
  # Tests write fixed fixture names. Separate working directories make -j safe.
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${test}_work")
  add_test(NAME ${test} COMMAND ${test})
  set_tests_properties(${test} PROPERTIES TIMEOUT 15 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${test}_work")
endforeach()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME assetc_regressions COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_assetc.py" $<TARGET_FILE:assetc>
    $<BOOL:${ASSET_PIPELINE_HAS_ZSTD}>)
  set_tests_properties(assetc_regressions PROPERTIES TIMEOUT 60)
  add_test(NAME assetprep_regressions COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_assetprep.py")
  set_tests_properties(assetprep_regressions PROPERTIES TIMEOUT 60)
  # The Pillow image decoder is an optional *authoring* dependency only.
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import PIL"
    RESULT_VARIABLE ASSET_PIPELINE_PILLOW_STATUS
    OUTPUT_QUIET ERROR_QUIET)
  if(ASSET_PIPELINE_PILLOW_STATUS EQUAL 0)
    add_test(NAME atlasprep_regressions COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/test_atlasprep.py")
    set_tests_properties(atlasprep_regressions PROPERTIES TIMEOUT 60)
  else()
    message(STATUS "Pillow unavailable: optional atlasprep tests not registered")
  endif()
else()
  message(STATUS "Python3 unavailable: assetc CLI regression suite not registered")
endif()

if(ASSET_PIPELINE_HAS_ZSTD)
  target_link_libraries(pak_safety PRIVATE ZSTD::ZSTD)
endif()
