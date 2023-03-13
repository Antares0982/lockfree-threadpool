cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(lockfree_threadpool LANGUAGES CXX)

add_library(common INTERFACE)

file(GLOB testpath ./src/*.cpp)

target_compile_options(common INTERFACE -Wall -Werror=return-type)
if (CMAKE_BUILD_TYPE MATCHES ".*Rel.*")
    target_compile_options(common INTERFACE -O3)
else ()
    target_compile_options(common INTERFACE -gdwarf-4)
endif ()
target_compile_options(common INTERFACE -pthread)
target_link_libraries(common INTERFACE pthread)
############################################################################
set(BUILD_TARGET lockfree_threadpool)
add_executable(${BUILD_TARGET} ${testpath})
set_target_properties(${BUILD_TARGET} PROPERTIES OUTPUT_NAME ${BUILD_TARGET})
target_link_libraries(${BUILD_TARGET} PRIVATE common)
