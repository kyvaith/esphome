set(esphome_sdmmc_patch "${CMAKE_CURRENT_LIST_DIR}/patch_sdmmc_transaction.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${esphome_sdmmc_patch}")
execute_process(
    COMMAND "${PYTHON}" "${esphome_sdmmc_patch}" "$ENV{IDF_PATH}"
    RESULT_VARIABLE esphome_sdmmc_patch_result
    COMMAND_ECHO STDOUT
)
if(NOT esphome_sdmmc_patch_result EQUAL 0)
    message(FATAL_ERROR
            "ESPHome SDMMC error-preservation patch failed: ${esphome_sdmmc_patch_result}")
endif()
idf_component_register()
