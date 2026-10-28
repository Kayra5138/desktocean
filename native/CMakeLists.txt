cmake_minimum_required(VERSION 3.21)
project(desktocean_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Qml Quick)
find_package(Threads REQUIRED)
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

# --- QML plugin: import Desktocean.Core --------------------------------------
qt_add_qml_module(desktocean_core
    URI Desktocean.Core
    VERSION 1.0
    PLUGIN_TARGET desktocean_core
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Desktocean/Core
    SOURCES
        src/cursor.h src/cursor.cpp
        src/simulation.h src/simulation.cpp
)

add_dependencies(desktocean_core desktocean_sim_cargo)
# The generated type-registration source includes our headers by bare name.
target_include_directories(desktocean_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(desktocean_core PRIVATE
    Qt6::Gui Qt6::Qml Qt6::Quick
    desktocean_sim Threads::Threads ${CMAKE_DL_LIBS} m
)
# Keep Rust's std symbols private so they can't clash with other plugins in plasmashell.
target_link_options(desktocean_core PRIVATE "LINKER:--exclude-libs,ALL")
