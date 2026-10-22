set(esphome_cache_restart_patch "${CMAKE_CURRENT_LIST_DIR}/patch_esp32p4_restart.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${esphome_cache_restart_patch}")
execute_process(
    COMMAND "${PYTHON}" "${esphome_cache_restart_patch}" "$ENV{IDF_PATH}"
    RESULT_VARIABLE esphome_cache_restart_patch_result
    COMMAND_ECHO STDOUT
)
if(NOT esphome_cache_restart_patch_result EQUAL 0)
    message(FATAL_ERROR
            "ESPHome ESP32-P4 restart patch failed: ${esphome_cache_restart_patch_result}")
endif()
idf_component_register()
