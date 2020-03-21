project(mag)
cmake_minimum_required(VERSION 2.6)

add_subdirectory(cz)

include_directories(cz/include)
include_directories(src)
include_directories(tracy)

set(LIBRARY_NAME ${PROJECT_NAME}l)
file(GLOB_RECURSE SRCS src/*.cpp)
add_library(${LIBRARY_NAME} ${SRCS})
target_include_directories(${LIBRARY_NAME} PUBLIC custom)

file(GLOB_RECURSE CLIENT_SRCS clients/*.cpp)
add_executable(${PROJECT_NAME} mains/main.cpp ${CLIENT_SRCS})
target_link_libraries(${PROJECT_NAME} custom)
target_link_libraries(${PROJECT_NAME} prose)
target_link_libraries(${PROJECT_NAME} clang_format)
target_link_libraries(${PROJECT_NAME} ${LIBRARY_NAME})
target_link_libraries(${PROJECT_NAME} cz)
target_include_directories(${PROJECT_NAME} PUBLIC clients)
target_link_libraries(${PROJECT_NAME} ncurses)
target_link_libraries(${PROJECT_NAME} tracy)
target_link_libraries(${PROJECT_NAME} pthread)
target_link_libraries(${PROJECT_NAME} dl)

file(GLOB_RECURSE CUSTOM_SRCS custom/*.cpp)
add_library(custom ${CUSTOM_SRCS})
target_include_directories(custom PUBLIC .)

file(GLOB_RECURSE PROSE_SRCS prose/*.cpp)
add_library(prose ${PROSE_SRCS})

file(GLOB_RECURSE CLANG_FORMAT_SRCS clang_format/*.cpp)
add_library(clang_format ${CLANG_FORMAT_SRCS})

if (!TRACY_ENABLE)
  file(GLOB_RECURSE TEST_SRCS tests/*.cpp)
  set(TEST_EXECUTABLE test)
  add_executable(${TEST_EXECUTABLE} ${TEST_SRCS})
  target_include_directories(${TEST_EXECUTABLE} PUBLIC src)
  target_include_directories(${TEST_EXECUTABLE} PUBLIC cz/include)
  target_include_directories(${TEST_EXECUTABLE} PUBLIC cz/test_base)
  target_link_libraries(${TEST_EXECUTABLE} ${LIBRARY_NAME})
  target_link_libraries(${TEST_EXECUTABLE} cz)
  target_link_libraries(${TEST_EXECUTABLE} czt)
  target_link_libraries(${TEST_EXECUTABLE} tracy)
endif()

add_library(tracy tracy/TracyClient.cpp)

if (CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror=switch")
endif()

if (TRACY_ENABLE)
  add_compile_definitions(TRACY_ENABLE)
endif()
