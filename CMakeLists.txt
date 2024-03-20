cmake_minimum_required( VERSION 3.1 )

project(
    libturbosqueeze
    VERSION 0.5
    LANGUAGES CXX)

include(CTest)
include(CheckCXXSourceCompiles)

set(AVX2_CXX_CODE_TEST "#if _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
int main() {
#if __AVX2__
return 0;
#else
#error \"AVX2 is not supported\"
#endif
}")

check_cxx_source_compiles("${AVX2_CXX_CODE_TEST}" AVX2_SUPPORTED)

if(AVX2_SUPPORTED)
set(
    AVX2_FILES
    turbosqueeze_decoder_avx2.cpp
    )
add_definitions(-mavx2)
else()
set(
    AVX2_FILES
    )
endif()


set(
    SOURCE_FILES
    libturbosqueeze.h
    turbosqueeze_context.h
    turbosqueeze_context.cpp
    turbosqueeze_decoder.cpp
    turbosqueeze_dict.cpp
    turbosqueeze_encoder.cpp
    platform.h)

add_library( turbosqueeze STATIC ${SOURCE_FILES} ${AVX2_FILES} )

add_subdirectory(sample)

# if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
# add_subdirectory(test)
# endif()

# enable_testing()

