set(esphome_mipi_dsi_patch
    "${CMAKE_CURRENT_LIST_DIR}/../patch_espidf_dsi_dma2d.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${esphome_mipi_dsi_patch}")

execute_process(
    COMMAND "${PYTHON}" "${esphome_mipi_dsi_patch}" "$ENV{IDF_PATH}"
    RESULT_VARIABLE esphome_mipi_dsi_patch_result
    COMMAND_ECHO STDOUT
)
if(NOT esphome_mipi_dsi_patch_result EQUAL 0)
    message(FATAL_ERROR
            "ESPHome MIPI DSI framework patch failed: ${esphome_mipi_dsi_patch_result}")
endif()

idf_component_register()
