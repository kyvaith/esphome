idf_component_register(REQUIRES lvgl)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(esphome_lvgl_patch "${CMAKE_CURRENT_LIST_DIR}/../lvgl_build_filter.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${esphome_lvgl_patch}")
    idf_component_get_property(esphome_lvgl_component_dir lvgl COMPONENT_DIR)
    execute_process(
        COMMAND "${PYTHON}" -c
                "import runpy, sys; runpy.run_path(sys.argv.pop(1), run_name='__main__')"
                "${esphome_lvgl_patch}"
                --patch-ppa-blocking-user-data "${esphome_lvgl_component_dir}"
        RESULT_VARIABLE esphome_lvgl_patch_result
        COMMAND_ECHO STDOUT
    )
    if(NOT esphome_lvgl_patch_result EQUAL 0)
        message(FATAL_ERROR "ESPHome LVGL PPA source patch failed: ${esphome_lvgl_patch_result}")
    endif()
endif()
