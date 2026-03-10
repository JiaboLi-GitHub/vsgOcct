add_library(vsgocct STATIC
    StepModelLoader.cpp
)

add_library(vsgocct::vsgocct ALIAS vsgocct)

set_target_properties(vsgocct PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)

target_include_directories(vsgocct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    PRIVATE
        ${OpenCASCADE_INCLUDE_DIR}
)

target_link_libraries(vsgocct
    PUBLIC
        vsg::vsg
    PRIVATE
        TKDESTEP
        TKMesh
        TKTopAlgo
)

target_compile_features(vsgocct PUBLIC cxx_std_17)
