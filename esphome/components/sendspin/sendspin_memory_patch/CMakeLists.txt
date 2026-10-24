idf_component_register(REQUIRES sendspin__sendspin-cpp)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(esphome_sendspin_patch "${CMAKE_CURRENT_LIST_DIR}/patch_sendspin_memory.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${esphome_sendspin_patch}")
    idf_component_get_property(esphome_sendspin_component_dir sendspin__sendspin-cpp COMPONENT_DIR)
    execute_process(
        COMMAND "${PYTHON}" "${esphome_sendspin_patch}" "${esphome_sendspin_component_dir}"
        RESULT_VARIABLE esphome_sendspin_patch_result
        COMMAND_ECHO STDOUT
    )
    if(NOT esphome_sendspin_patch_result EQUAL 0)
        message(FATAL_ERROR "ESPHome Sendspin memory patch failed: ${esphome_sendspin_patch_result}")
    endif()
endif()
