cmake_minimum_required(VERSION 3.21)
project(SandSimulation_Raylib C)

set(CMAKE_C_STANDARD 23)

include_directories(${CMAKE_SOURCE_DIR}/Raylib/include)
find_package(SQLite3)
add_executable(SandSimulation_Raylib main.c Application.c Application.h)
link_directories(${CMAKE_SOURCE_DIR}/Raylib)
target_link_libraries(SandSimulation_Raylib ${CMAKE_SOURCE_DIR}/Raylib/libraylib.a ${CMAKE_SOURCE_DIR}/Raylib/libglfw3.a opengl32 gdi32 winmm)
target_link_libraries(SandSimulation_Raylib SQLITE3)