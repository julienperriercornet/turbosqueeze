project(tsq LANGUAGES CXX)

add_executable(tsq turbosqueeze.cpp)

target_link_libraries(tsq PRIVATE turbosqueeze)
