add_library(asset_runtime STATIC
  pak_reader.cpp pak_writer.cpp hash.cpp xxhash64.cpp hash128.cpp compression_zstd.cpp
  "${xxhash_SOURCE_DIR}/xxhash.c")
add_library(AssetPipeline::asset_runtime ALIAS asset_runtime)
asset_pipeline_configure_target(asset_runtime)
set_target_properties(asset_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(asset_runtime
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${xxhash_SOURCE_DIR}")
target_link_libraries(asset_runtime PRIVATE BLAKE3::blake3)
target_compile_definitions(asset_runtime PRIVATE ASSET_HAS_ZSTD=$<BOOL:${ASSET_PIPELINE_HAS_ZSTD}>)
if(ASSET_PIPELINE_HAS_ZSTD)
  target_link_libraries(asset_runtime PRIVATE ZSTD::ZSTD)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS asset_runtime EXPORT asset_pipelineTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ../include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT asset_pipelineTargets FILE asset_pipelineTargets.cmake
  NAMESPACE AssetPipeline:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AssetPipeline)
configure_package_config_file(../cmake/AssetPipelineConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AssetPipeline)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/AssetPipelineConfigVersion.cmake
              ../cmake/FindZSTD.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AssetPipeline)
