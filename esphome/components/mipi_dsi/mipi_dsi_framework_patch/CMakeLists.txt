set(esphome_mipi_dsi_patch
    "${CMAKE_CURRENT_LIST_DIR}/../patch_espidf_dsi_dma2d.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${esphome_mipi_dsi_patch}"
             "${CMAKE_CURRENT_LIST_DIR}/../ppa_wait_diagnostics.py")

set(esphome_mipi_dsi_diagnostic_args)
if(CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS)
    list(APPEND esphome_mipi_dsi_diagnostic_args --ppa-wait-diagnostics)
endif()

execute_process(
    COMMAND "${PYTHON}" "${esphome_mipi_dsi_patch}" "$ENV{IDF_PATH}"
            ${esphome_mipi_dsi_diagnostic_args}
    RESULT_VARIABLE esphome_mipi_dsi_patch_result
    COMMAND_ECHO STDOUT
)
if(NOT esphome_mipi_dsi_patch_result EQUAL 0)
    message(FATAL_ERROR
            "ESPHome MIPI DSI framework patch failed: ${esphome_mipi_dsi_patch_result}")
endif()

idf_component_register()
