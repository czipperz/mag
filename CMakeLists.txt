project(mag)
cmake_minimum_required(VERSION 2.6)

add_subdirectory(cz)

include_directories(cz/include)
include_directories(src)

set(LIBRARY_NAME ${PROJECT_NAME}l)
file(GLOB_RECURSE SRCS src/*.cpp)
add_library(${LIBRARY_NAME} ${SRCS})

file(GLOB_RECURSE CLIENT_SRCS clients/*.cpp)
add_executable(${PROJECT_NAME} mains/main.cpp ${CLIENT_SRCS})
target_link_libraries(${PROJECT_NAME} ${LIBRARY_NAME})
target_link_libraries(${PROJECT_NAME} cz)
target_include_directories(${PROJECT_NAME} PUBLIC clients)
target_link_libraries(${PROJECT_NAME} ncurses)

file(GLOB_RECURSE TEST_SRCS tests/*.cpp)
set(TEST_EXECUTABLE test)
add_executable(${TEST_EXECUTABLE} ${TEST_SRCS})
target_include_directories(${TEST_EXECUTABLE} PUBLIC src)
target_include_directories(${TEST_EXECUTABLE} PUBLIC cz/include)
target_include_directories(${TEST_EXECUTABLE} PUBLIC cz/test_base)
target_link_libraries(${TEST_EXECUTABLE} ${LIBRARY_NAME})
target_link_libraries(${TEST_EXECUTABLE} cz)
target_link_libraries(${TEST_EXECUTABLE} czt)

if (CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror=switch")
endif()
