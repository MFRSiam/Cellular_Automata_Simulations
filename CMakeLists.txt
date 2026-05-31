cmake_minimum_required(VERSION 3.21)
project(SandSimulation_Raylib C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Fetch raylib (latest release) and build it as part of this project, instead
# of relying on a vendored copy.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# raylib build options: we only need the library, not its examples/games.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5          # latest stable release
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(${PROJECT_NAME} main.c Application.c Application.h)

# raylib brings its modules (raymath, rlgl, etc.) and platform libs with it.
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# On Windows, link the system libs raylib needs (handled automatically by the
# raylib target on MSVC, listed here for completeness on other toolchains).
if (APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif ()
