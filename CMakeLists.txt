cmake_minimum_required(VERSION 3.13)
project(upmem_interface LANGUAGES C CXX)
set(CMAKE_VERBOSE_MAKEFILE TRUE)
set(CMAKE_C_COMPILER "g++")
add_library(upmem_direct_c STATIC upmem_direct_c.c)

set(UPMEM_SDK_DIR "$ENV{UPMEM_SDK_DIR}")
set(INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/parlaylib/include" 
                "${CMAKE_CURRENT_SOURCE_DIR}/pim_interface"
                "${CMAKE_CURRENT_SOURCE_DIR}/pim_interface/sdk_internals_2023.2.0"
                "${UPMEM_SDK_DIR}/include/dpu"
                ".")
target_include_directories(upmem_direct_c PRIVATE ${INCLUDE_DIR})
target_link_directories(upmem_direct_c PRIVATE ${UPMEM_SDK_DIR}/lib)
target_link_libraries(upmem_direct_c PRIVATE -ldpu)
target_compile_options(upmem_direct_c PRIVATE -Wall -Wextra -O3 -std=c++17 -march=native -pthread)
set_target_properties(upmem_direct_c PROPERTIES PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/upmem_direct_c.h)

install(TARGETS upmem_direct_c 
        ARCHIVE DESTINATION lib)
install(FILES upmem_direct_c.h
        DESTINATION include)