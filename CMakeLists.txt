cmake_minimum_required( VERSION 3.1 )

option(AVX2 "Build with Avx2 optimisations" OFF)

project(
    libturbosqueeze
    VERSION 0.5
    LANGUAGES CXX)

include(CTest)

if (AVX2)
set(
    AVX2_FILES
    turbosqueeze_decoder_avx2.cpp
    )
add_definitions(-mavx2)
add_definitions(-DAVX2)
message( "AVX2 supported" )
else()
set(
    AVX2_FILES
    )
message( "No AVX2 support" )
endif()


set(
    SOURCE_FILES
    libturbosqueeze.h
    turbosqueeze_context.h
    turbosqueeze_context.cpp
    turbosqueeze_decoder.cpp
    turbosqueeze_dict.cpp
    turbosqueeze_encoder.cpp
    turbosqueeze_api.cpp
    platform.h)

add_library( turbosqueeze STATIC ${SOURCE_FILES} ${AVX2_FILES} )

add_subdirectory(sample)

# if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
# add_subdirectory(test)
# endif()

# enable_testing()

