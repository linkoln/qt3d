# Generated from simplecustommaterial.pro.

cmake_minimum_required(VERSION 3.14)
project(simplecustommaterial LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(INSTALL_EXAMPLEDIR "examples")

find_package(Qt6 COMPONENTS Core)
find_package(Qt6 COMPONENTS Gui)
find_package(Qt6 COMPONENTS Qml)
find_package(Qt6 COMPONENTS Quick)

add_qt_gui_executable(simplecustommaterial
    main.cpp
)
target_link_libraries(simplecustommaterial PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Qml
    Qt::Quick
)


# Resources:
set(qml_resource_files
    "PlaneModel.qml"
    "SceneRoot.qml"
    "SimpleMaterial.qml"
    "main.qml"
)

qt6_add_resources(simplecustommaterial "qml"
    PREFIX
        "/"
    FILES
        ${qml_resource_files}
)
set(shaders_resource_files
    "shaders/es2/simpleColor.frag"
    "shaders/es2/simpleColor.vert"
    "shaders/gl3/simpleColor.frag"
    "shaders/gl3/simpleColor.vert"
)

qt6_add_resources(simplecustommaterial "shaders"
    PREFIX
        "/"
    FILES
        ${shaders_resource_files}
)

install(TARGETS simplecustommaterial
    RUNTIME DESTINATION "${INSTALL_EXAMPLEDIR}"
    BUNDLE DESTINATION "${INSTALL_EXAMPLEDIR}"
    LIBRARY DESTINATION "${INSTALL_EXAMPLEDIR}"
)
