cmake_minimum_required(VERSION 3.21)
project(SandSimulation_Raylib C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Graphics backend: build raylib against a modern desktop OpenGL (4.3 core).
# This also makes raylib emit GLSL 4.30 default shaders, matching ours.
# Set before raylib is configured.
# ---------------------------------------------------------------------------
set(OPENGL_VERSION "4.3" CACHE STRING "OpenGL version for raylib" FORCE)

# ---------------------------------------------------------------------------
# Fetch raylib (latest stable) + raygui (immediate-mode UI) and build them as
# part of this project.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# raygui is header-only; just grab the sources and put its header on the path.
# (Populate rather than MakeAvailable so we don't build its example targets.)
FetchContent_Declare(
    raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG        4.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(raygui)
if (NOT raygui_POPULATED)
    FetchContent_Populate(raygui)
endif ()

# ---------------------------------------------------------------------------
# Threads (Win32 / pthreads) for the background simulation worker.
# ---------------------------------------------------------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Our executable: every .c under src/, which is organised into modules:
#   src/core   - shared defs, config, threading, audio (platform/infra)
#   src/world  - materials, noise, biomes, grid sim, structures, sim worker
#   src/render - parallax + post-processing pipeline
#   src/ui     - HUD, menus, structure manager (raygui)
# Headers are included by bare name, so every module dir is on the include
# path and source files don't need path-qualified includes.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")
add_executable(${PROJECT_NAME} ${APP_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/world"
    "${CMAKE_SOURCE_DIR}/src/render"
    "${CMAKE_SOURCE_DIR}/src/ui"
    "${raygui_SOURCE_DIR}/src")
target_link_libraries(${PROJECT_NAME} PRIVATE raylib Threads::Threads)

if (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif ()

# ---------------------------------------------------------------------------
# Copy runtime resources (shaders / config / assets) next to the executable so
# they load via GetApplicationDirectory().
# ---------------------------------------------------------------------------
foreach (RES shaders config assets structures)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/${RES}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${RES}"
        COMMENT "Copying ${RES}/ to output directory")
endforeach ()
