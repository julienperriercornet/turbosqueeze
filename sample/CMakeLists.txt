project(tsq LANGUAGES CXX)

add_executable(tsq tsq.cpp)

target_link_libraries(tsq PRIVATE turbosqueeze)
