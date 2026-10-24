idf_component_register(REQUIRES lvgl__lvgl)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(esphome_lvgl_patch "${CMAKE_CURRENT_LIST_DIR}/../lvgl_build_filter.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${esphome_lvgl_patch}")
    idf_component_get_property(esphome_lvgl_component_dir lvgl__lvgl COMPONENT_DIR)
    execute_process(
        COMMAND "${PYTHON}" -c
                "import runpy, sys; runpy.run_path(sys.argv.pop(1), run_name='__main__')"
                "${esphome_lvgl_patch}"
                --patch-component-sources "${esphome_lvgl_component_dir}"
        RESULT_VARIABLE esphome_lvgl_patch_result
        COMMAND_ECHO STDOUT
    )
    if(NOT esphome_lvgl_patch_result EQUAL 0)
        message(FATAL_ERROR "ESPHome LVGL source patches failed: ${esphome_lvgl_patch_result}")
    endif()
    execute_process(
        COMMAND "${PYTHON}" -c
                "import runpy, sys; runpy.run_path(sys.argv.pop(1), run_name='__main__')"
                "${esphome_lvgl_patch}"
                --patch-espidf-ppa-cache-sync "$ENV{IDF_PATH}"
        RESULT_VARIABLE esphome_lvgl_ppa_patch_result
        COMMAND_ECHO STDOUT
    )
    if(NOT esphome_lvgl_ppa_patch_result EQUAL 0)
        message(FATAL_ERROR "ESPHome PPA cache source patches failed: ${esphome_lvgl_ppa_patch_result}")
    endif()
endif()
