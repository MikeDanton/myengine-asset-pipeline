add_executable(assetc main.cpp)
asset_pipeline_configure_target(assetc)
target_link_libraries(assetc PRIVATE AssetPipeline::asset_runtime BLAKE3::blake3)
install(TARGETS assetc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
