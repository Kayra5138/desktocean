cmake_minimum_required(VERSION 3.21)
project(desktocean_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Multimedia Network Qml Quick QuickControls2 ShaderTools Widgets)
find_package(Threads REQUIRED)
# Idle sleep: input idle time and screen power (both work on Wayland)
find_package(KF6IdleTime REQUIRED)
find_package(KF6Screen REQUIRED)
# "Feed fish at cursor" keyboard shortcut
find_package(KF6GlobalAccel REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS DBus)
qt_standard_project_setup(REQUIRES 6.5)

# --- Rust simulation core (static library built by cargo) --------------------
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sim)
set(SIM_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(SIM_LIB ${SIM_TARGET_DIR}/release/libdesktocean_sim.a)

add_custom_target(desktocean_sim_cargo
    COMMAND cargo build --release --manifest-path ${SIM_DIR}/Cargo.toml --target-dir ${SIM_TARGET_DIR}
    BYPRODUCTS ${SIM_LIB}
    WORKING_DIRECTORY ${SIM_DIR}
    COMMENT "Building Rust simulation core"
    USES_TERMINAL
)

add_library(desktocean_sim STATIC IMPORTED)
set_target_properties(desktocean_sim PROPERTIES
    IMPORTED_LOCATION ${SIM_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${SIM_DIR}/include
)

# --- Fish library on disk and fish mesh (shared by the plugin and the editor) -
add_library(desktocean_library STATIC
    src/fishlibrary.h src/fishlibrary.cpp
    src/fishmesh.h src/fishmesh.cpp
    src/themes.h src/themes.cpp
)
set_target_properties(desktocean_library PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)
target_include_directories(desktocean_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(desktocean_library PUBLIC Qt6::Gui)

# --- Sprite batch rendering (shared by the plugin and the editor preview) -----
add_library(desktocean_render STATIC src/spritenode.h src/spritenode.cpp)
set_target_properties(desktocean_render PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC OFF)
target_link_libraries(desktocean_render PUBLIC Qt6::Quick desktocean_library)
qt_add_shaders(desktocean_render "desktocean_render_shaders"
    PREFIX "/desktocean"
    FILES
        shaders/fishbatch.vert
        shaders/fishbatch.frag
)

# --- QML plugin: import Desktocean.Core --------------------------------------
qt_add_qml_module(desktocean_core
    URI Desktocean.Core
    VERSION 1.0
    PLUGIN_TARGET desktocean_core
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Desktocean/Core
    SOURCES
        src/fishtank.h src/fishtank.cpp
        src/tankhub.h src/tankhub.cpp
        src/sounds.h src/sounds.cpp
        src/idlewatcher.h src/idlewatcher.cpp
)


add_dependencies(desktocean_core desktocean_sim_cargo)
# The generated type-registration source includes our headers by bare name.
target_include_directories(desktocean_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(desktocean_core PRIVATE
    Qt6::Gui Qt6::Multimedia Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::DBus KF6::IdleTime KF6::ScreenDpms KF6::GlobalAccel
    desktocean_sim desktocean_render desktocean_library Threads::Threads ${CMAKE_DL_LIBS} m
)
# Keep Rust's std symbols private so they can't clash with other plugins in plasmashell.
target_link_options(desktocean_core PRIVATE "LINKER:--exclude-libs,ALL")

# --- Drawing editor: desktocean-editor ----------------------------------------
qt_add_executable(desktocean-editor editor/main.cpp)
qt_add_qml_module(desktocean-editor
    URI Desktocean.Editor
    VERSION 1.0
    QML_FILES
        editor/qml/Main.qml
        editor/qml/DrawPage.qml
        editor/qml/LibraryPage.qml
        editor/qml/TankLayoutPage.qml
        editor/qml/FamilyTreePage.qml
    SOURCES
        editor/drawingcanvas.h editor/drawingcanvas.cpp
        editor/librarymodel.h editor/librarymodel.cpp
        editor/swimpreview.h editor/swimpreview.cpp
        editor/layoutmodel.h editor/layoutmodel.cpp
        editor/appinstance.h editor/appinstance.cpp
)
target_include_directories(desktocean-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/editor)
target_link_libraries(desktocean-editor PRIVATE
    Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Network desktocean_render desktocean_library
)
