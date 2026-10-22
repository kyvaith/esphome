set(esphome_pie_context_patch "${CMAKE_CURRENT_LIST_DIR}/patch_pie_context.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${esphome_pie_context_patch}")
execute_process(
    COMMAND "${PYTHON}" "${esphome_pie_context_patch}" "$ENV{IDF_PATH}" "${IDF_TARGET}"
    RESULT_VARIABLE esphome_pie_context_patch_result
    COMMAND_ECHO STDOUT
)
if(NOT esphome_pie_context_patch_result EQUAL 0)
    message(FATAL_ERROR
            "ESPHome ESP32-P4 PIE context patch failed: ${esphome_pie_context_patch_result}")
endif()
idf_component_register()
