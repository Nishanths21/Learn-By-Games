cmake_minimum_required(VERSION 3.10)
project(LearnByGames)

set(CMAKE_CXX_STANDARD 17)

# Point to the "include" folder inside the code we just cloned
include_directories(include/crow_source/include)

add_executable(LearnByGames main.cpp)

target_link_libraries(LearnByGames pthread sqlite3)