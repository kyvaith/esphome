set(esphome_fast_heap_patch "${CMAKE_CURRENT_LIST_DIR}/patch_fast_heap_query.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${esphome_fast_heap_patch}" "${CMAKE_CURRENT_LIST_DIR}/patches.json")
execute_process(
    COMMAND "${PYTHON}" "${esphome_fast_heap_patch}" "$ENV{IDF_PATH}"
    RESULT_VARIABLE esphome_fast_heap_patch_result
    COMMAND_ECHO STDOUT
)
if(NOT esphome_fast_heap_patch_result EQUAL 0)
    message(FATAL_ERROR
            "ESPHome fast heap query patch failed: ${esphome_fast_heap_patch_result}")
endif()
idf_component_register()
