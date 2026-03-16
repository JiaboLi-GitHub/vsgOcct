set(VSGOCCT_SOURCES
    StepModelLoader.cpp
    cad/StepReader.cpp
    mesh/ShapeMesher.cpp
    pick/PickHandler.cpp
    pick/SelectionManager.cpp
    scene/SceneBuilder.cpp
)

set(VSGOCCT_HEADERS
    ../../include/vsgocct/StepModelLoader.h
    ../../include/vsgocct/cad/StepReader.h
    ../../include/vsgocct/mesh/ShapeMesher.h
    ../../include/vsgocct/pick/PickHandler.h
    ../../include/vsgocct/pick/SelectionManager.h
    ../../include/vsgocct/scene/SceneBuilder.h
)

add_library(vsgocct STATIC
    ${VSGOCCT_SOURCES}
    ${VSGOCCT_HEADERS}
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
        ${OpenCASCADE_INCLUDE_DIR}
)

target_link_libraries(vsgocct
    PUBLIC
        vsg::vsg
    PRIVATE
        TKDESTEP
        TKLCAF
        TKXCAF
        TKMesh
        TKTopAlgo
)

target_compile_features(vsgocct PUBLIC cxx_std_17)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${VSGOCCT_SOURCES})
source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    PREFIX "Header Files"
    FILES ${VSGOCCT_HEADERS}
)
