cmake_minimum_required( VERSION 3.1 )

project(
    libturbosqueeze
    VERSION 0.1
    LANGUAGES CXX)

include(CTest)

set(
    SOURCE_FILES
    libturbosqueeze.h
    turbosqueeze_context.h
    turbosqueeze_context.cpp
    turbosqueeze_decoder.cpp
    turbosqueeze_dict.cpp
    turbosqueeze_encoder.cpp
    platform.h)

add_library( turbosqueeze STATIC ${SOURCE_FILES} )

add_subdirectory(sample)

# if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
# add_subdirectory(test)
# endif()

# enable_testing()

