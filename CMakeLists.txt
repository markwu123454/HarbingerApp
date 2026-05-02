cmake_minimum_required(VERSION 4.1)
project(HarbingerApp)

set(CMAKE_CXX_STANDARD 20)

# Qt setup (important)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "D:/Qt/6.11.0/mingw_64/lib/cmake")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(HarbingerApp main.cpp)

target_link_libraries(HarbingerApp Qt6::Widgets)