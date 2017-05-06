cmake_minimum_required(VERSION 2.8)
project(S17FS)
set(CMAKE_COLOR_MAKEFILE ON)

if(APPLE)
    # Make policy CMP0042 warning go away
    # even though I still don't quite understand it
    set(CMAKE_MACOSX_RPATH ON)
endif(APPLE)

include_directories(include)
add_library(bitmap SHARED src/bitmap.c)
add_library(back_store SHARED src/block_store.c)
add_library(dyn_array SHARED src/dyn_array.c)
add_library(backend SHARED src/backend.c)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS} include)

# add -DENABLE_MOVE to the CXX flags to activate move tests
set(SHARED_FLAGS " -Wall -Wextra -Wshadow -Werror -g -D_POSIX_C_SOURCE=200809L")
set(CMAKE_CXX_FLAGS "-std=c++11 ${SHARED_FLAGS}")
set(CMAKE_C_FLAGS "-std=c99 ${SHARED_FLAGS}")

add_library(S17FS SHARED src/S17FS.c)
set_target_properties(S17FS PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(S17FS back_store dyn_array bitmap backend)

add_executable(fs_test test/tests.cpp)
target_link_libraries(fs_test S17FS ${GTEST_LIBRARIES} pthread)

#install(TARGETS S17FS DESTINATION lib)
#install(FILES include/S17FS.h DESTINATION include)
#enable_testing()
#add_test(NAME    fs_test 
#         COMMAND fs_test)
