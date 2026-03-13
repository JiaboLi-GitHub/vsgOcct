add_executable(vsgqt_step_viewer
    main.cpp
)

target_link_libraries(vsgqt_step_viewer
    PRIVATE
        vsgocct::vsgocct
        vsgQt::vsgQt
)

vsgocct_copy_runtime_dlls(vsgqt_step_viewer)

vsgocct_deploy_qt(vsgqt_step_viewer)
