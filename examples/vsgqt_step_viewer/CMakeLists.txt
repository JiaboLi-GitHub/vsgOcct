add_executable(vsgqt_step_viewer
    main.cpp
)

target_link_libraries(vsgqt_step_viewer
    PRIVATE
        vsgocct::vsgocct
        vsgQt::vsgQt
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    add_custom_command(TARGET vsgqt_step_viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:vsgqt_step_viewer> $<TARGET_RUNTIME_DLLS:vsgqt_step_viewer>
        COMMAND_EXPAND_LISTS
    )
endif()

vsgocct_deploy_qt(vsgqt_step_viewer)
