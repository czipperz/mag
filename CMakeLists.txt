project(mag)
cmake_minimum_required(VERSION 2.6)

add_subdirectory(cz)

include_directories(cz/include)
include_directories(.)
include_directories(src)
include_directories(tracy)

if (WIN32)
  set(SDL2_LIBRARIES ${CMAKE_SOURCE_DIR}/SDL/lib/x64/SDL2.lib)
  set(SDL2_DLLS ${CMAKE_SOURCE_DIR}/SDL/lib/x64/SDL2.dll)
  set(SDL2_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/SDL/include)
  set(SDL2_TTF_LIBRARIES ${CMAKE_SOURCE_DIR}/TTF/lib/x64/SDL2_ttf.lib)
  set(SDL2_TTF_DLLS ${CMAKE_SOURCE_DIR}/TTF/lib/x64/SDL2_ttf.dll ${CMAKE_SOURCE_DIR}/TTF/lib/x64/libfreetype-6.dll ${CMAKE_SOURCE_DIR}/TTF/lib/x64/zlib1.dll)
  set(SDL2_TTF_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/TTF/include)
  set(SDL2_IMAGE_LIBRARIES ${CMAKE_SOURCE_DIR}/IMG/lib/x64/SDL2_image.lib)
  set(SDL2_IMAGE_DLLS ${CMAKE_SOURCE_DIR}/IMG/lib/x64/SDL2_image.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libjpeg-9.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libpng16-16.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libtiff-5.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libwebp-7.dll)
  set(SDL2_IMAGE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/IMG/include)

  add_custom_target(sdl2_dlls ALL
      ${CMAKE_COMMAND} -E copy_if_different ${SDL2_DLLS} ${CMAKE_BINARY_DIR})
  add_custom_target(sdl2_ttf_dlls ALL
      ${CMAKE_COMMAND} -E copy_if_different ${SDL2_TTF_DLLS} ${CMAKE_BINARY_DIR})
  add_custom_target(sdl2_image_dlls ALL
      ${CMAKE_COMMAND} -E copy_if_different ${SDL2_IMAGE_DLLS} ${CMAKE_BINARY_DIR})

  set(WINDOWS_RC_FILE "res/Windows_RC.rc")
else()
  set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
  find_package(SDL2 REQUIRED)
  find_package(SDL2_image REQUIRED)
  find_package(SDL2_ttf REQUIRED)
  set(HAS_NCURSES 1)

  set(WINDOWS_RC_FILE)
endif()

set(LIBRARY_NAME ${PROJECT_NAME}l)
file(GLOB_RECURSE SRCS src/*.cpp)
add_library(${LIBRARY_NAME} ${SRCS})
target_include_directories(${LIBRARY_NAME} PUBLIC custom)

file(GLOB_RECURSE CLIENT_SRCS clients/*.cpp)
add_executable(${PROJECT_NAME} WIN32 mains/main.cpp ${CLIENT_SRCS} ${WINDOWS_RC_FILE})
target_link_libraries(${PROJECT_NAME} custom)
target_link_libraries(${PROJECT_NAME} basic)
target_link_libraries(${PROJECT_NAME} prose)
target_link_libraries(${PROJECT_NAME} clang_format)
target_link_libraries(${PROJECT_NAME} git)
target_link_libraries(${PROJECT_NAME} syntax)
target_link_libraries(${PROJECT_NAME} man)
target_link_libraries(${PROJECT_NAME} render)
target_link_libraries(${PROJECT_NAME} ${LIBRARY_NAME})
target_link_libraries(${PROJECT_NAME} cz)
target_include_directories(${PROJECT_NAME} PUBLIC clients render)

if (${HAS_NCURSES})
  target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_NCURSES=1)
  target_link_libraries(${PROJECT_NAME} ncurses)
endif()
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} tracy)

if (NOT WIN32)
  target_link_libraries(${PROJECT_NAME} pthread)
  target_link_libraries(${PROJECT_NAME} dl)
endif()

if (WIN32)
  add_custom_target(icon COMMAND magick convert ${CMAKE_SOURCE_DIR}/res/logo.png -define icon:auto-resize=256,128,96,64,48,32,16 ${CMAKE_SOURCE_DIR}/res/logo.ico
                    DEPENDS ${CMAKE_SOURCE_DIR}/res/logo.png)
else()
  add_custom_target(icon COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/logo.png ${CMAKE_BINARY_DIR}
                    DEPENDS ${CMAKE_SOURCE_DIR}/res/logo.png)
endif()
add_dependencies(${PROJECT_NAME} icon)

if (WIN32)
  add_custom_target(copy_resources ALL
      ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/diff.exe ${CMAKE_SOURCE_DIR}/res/diff_COPYING ${CMAKE_SOURCE_DIR}/res/libiconv2.dll ${CMAKE_SOURCE_DIR}/res/libintl3.dll ${CMAKE_BINARY_DIR})
else()
  # diff is assumed to be installed on linux
endif()

file(GLOB_RECURSE CUSTOM_SRCS custom/*.cpp)
add_library(custom ${CUSTOM_SRCS})

file(GLOB_RECURSE BASIC_SRCS basic/*.cpp)
add_library(basic ${BASIC_SRCS})

file(GLOB_RECURSE PROSE_SRCS prose/*.cpp)
add_library(prose ${PROSE_SRCS})

file(GLOB_RECURSE CLANG_FORMAT_SRCS clang_format/*.cpp)
add_library(clang_format ${CLANG_FORMAT_SRCS})

file(GLOB_RECURSE GIT_SRCS git/*.cpp)
add_library(git ${GIT_SRCS})

file(GLOB_RECURSE SYNTAX_SRCS syntax/*.cpp)
add_library(syntax ${SYNTAX_SRCS})

file(GLOB_RECURSE MAN_SRCS man/*.cpp)
add_library(man ${MAN_SRCS})

file(GLOB_RECURSE RENDER_SRCS render/*.cpp)
add_library(render ${RENDER_SRCS})

if (TRACY_ENABLE)
  add_compile_definitions(TRACY_ENABLE)
else()
  file(GLOB_RECURSE TEST_SRCS tests/*.cpp)
  set(TEST_EXECUTABLE test)
  add_executable(${TEST_EXECUTABLE} ${TEST_SRCS})
  target_include_directories(${TEST_EXECUTABLE} PUBLIC src)
  target_include_directories(${TEST_EXECUTABLE} PUBLIC cz/include)
  target_include_directories(${TEST_EXECUTABLE} PUBLIC cz/test_base)
  target_link_libraries(${TEST_EXECUTABLE} basic)
  target_link_libraries(${TEST_EXECUTABLE} ${LIBRARY_NAME})
  target_link_libraries(${TEST_EXECUTABLE} custom)
  target_link_libraries(${TEST_EXECUTABLE} basic)
  target_link_libraries(${TEST_EXECUTABLE} prose)
  target_link_libraries(${TEST_EXECUTABLE} clang_format)
  target_link_libraries(${TEST_EXECUTABLE} git)
  target_link_libraries(${TEST_EXECUTABLE} syntax)
  target_link_libraries(${TEST_EXECUTABLE} man)
  target_link_libraries(${TEST_EXECUTABLE} ${LIBRARY_NAME})
  target_link_libraries(${TEST_EXECUTABLE} cz)
  target_link_libraries(${TEST_EXECUTABLE} czt)
  target_link_libraries(${TEST_EXECUTABLE} tracy)
endif()

add_library(tracy tracy/TracyClient.cpp)

if (CMAKE_COMPILER_IS_GNUCC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror=switch")
endif()
