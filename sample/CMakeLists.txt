cmake_minimum_required(VERSION 3.15)
project(samples LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")

add_executable(tsq main.cpp)
# target_compile_options(tsq PRIVATE "-fsanitize=address")
# target_link_options(tsq PRIVATE "-fsanitize=address")
target_link_libraries(tsq PRIVATE turbosqueeze)
