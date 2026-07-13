cmake_minimum_required(VERSION 3.15)

project(
    turbosqueeze
    VERSION 3.0
    LANGUAGES CXX)

option(AVX2 "Build with AVX2 optimisations" OFF)
option(ENABLE_ASAN "Build with AddressSanitizer instrumentation" OFF)

include(CTest)

if (ENABLE_ASAN)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
add_link_options(-fsanitize=address)
message("AddressSanitizer enabled")
else()
message(FATAL_ERROR "ENABLE_ASAN is only supported with GCC and Clang")
endif()
endif()

if (AVX2)
add_definitions(-mavx2)
add_definitions(-DAVX2)
message( "AVX2 enabled" )
else()
message( "AVX2 disabled" )
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

set(
    SOURCE_FILES
    turbosqueeze.h
    tsq_common.h
    tsq_context.h
    tsq_context.cpp
    tsq_encode.h
    tsq_encode.cpp
    tsq_decode.h
    tsq_decode.cpp
    tsq_encode2.cpp
    tsq_encode2_hist.cpp
    tsq_encode2_opt.cpp
    tsq_encode3.cpp
    tsq_encode3_fast.cpp
    tsq_decode2.cpp
    tsq_decode3.cpp
    tsq_entropy.cpp
    tsq_entropy.h
    platform.h
    turbosqueeze.cpp
    tsq_threads.cpp)

add_library( turbosqueeze STATIC ${SOURCE_FILES} )
target_link_libraries( turbosqueeze PUBLIC Threads::Threads )

add_subdirectory(sample)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(test)
endif()

enable_testing()

