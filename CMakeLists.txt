# Mag build script
#
# To add a new package go the line starting with `file(GLOB_RECURSE SRCS` and add your sources to
# the list of globs.

cmake_minimum_required(VERSION 3.3)

project(mag)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define TRACY_INCLUDE_DIRS for cz to use.
set(TRACY_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/tracy)

add_subdirectory(cz)

include_directories(cz/include)
include_directories(.)
include_directories(src)
include_directories(${TRACY_INCLUDE_DIRS})

if (WIN32)
  # Fake find_package results
  set(SDL2_LIBRARIES ${CMAKE_SOURCE_DIR}/SDL/lib/x64/SDL2.lib)
  set(SDL2_DLLS ${CMAKE_SOURCE_DIR}/SDL/lib/x64/SDL2.dll)
  set(SDL2_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/SDL/include)
  set(SDL2_TTF_LIBRARIES ${CMAKE_SOURCE_DIR}/TTF/lib/x64/SDL2_ttf.lib)
  set(SDL2_TTF_DLLS ${CMAKE_SOURCE_DIR}/TTF/lib/x64/SDL2_ttf.dll ${CMAKE_SOURCE_DIR}/TTF/lib/x64/libfreetype-6.dll ${CMAKE_SOURCE_DIR}/TTF/lib/x64/zlib1.dll)
  set(SDL2_TTF_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/TTF/include)
  set(SDL2_IMAGE_LIBRARIES ${CMAKE_SOURCE_DIR}/IMG/lib/x64/SDL2_image.lib)
  set(SDL2_IMAGE_DLLS ${CMAKE_SOURCE_DIR}/IMG/lib/x64/SDL2_image.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libjpeg-9.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libpng16-16.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libtiff-5.dll ${CMAKE_SOURCE_DIR}/IMG/lib/x64/libwebp-7.dll)
  set(SDL2_IMAGE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/IMG/include)

  # Copy in the dlls
  add_custom_target(sdl2_dlls ALL
      ${CMAKE_COMMAND} -E copy_if_different ${SDL2_DLLS} ${SDL2_TTF_DLLS} ${SDL2_IMAGE_DLLS} ${CMAKE_BINARY_DIR})

  # RC file sets the app icon, needs to be included as a source file.
  set(WINDOWS_RC_FILE "res/Windows_RC.rc")
else()
  set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
  find_package(SDL2 REQUIRED)
  find_package(SDL2_image REQUIRED)
  find_package(SDL2_ttf REQUIRED)
  set(HAS_NCURSES 1)

  set(WINDOWS_RC_FILE)
endif()

# Build all sources as library magl (so we don't double build to make testing program)
set(LIBRARY_NAME ${PROJECT_NAME}l)
file(GLOB_RECURSE SRCS src/*.cpp custom/*.cpp basic/*.cpp prose/*.cpp clang_format/*.cpp git/*.cpp
                       gnu_global/*.cpp syntax/*.cpp man/*.cpp render/*.cpp)
add_library(${LIBRARY_NAME} ${SRCS})
target_include_directories(${LIBRARY_NAME} PUBLIC custom)
target_compile_definitions(${LIBRARY_NAME} PUBLIC MAG_BUILD_DIRECTORY=${CMAKE_SOURCE_DIR})

# Build mag (mag.exe)
file(GLOB_RECURSE CLIENT_SRCS clients/*.cpp)
add_executable(${PROJECT_NAME} WIN32 mains/main.cpp ${CLIENT_SRCS} ${WINDOWS_RC_FILE})
target_link_libraries(${PROJECT_NAME} ${LIBRARY_NAME})
target_link_libraries(${PROJECT_NAME} cz)
target_include_directories(${PROJECT_NAME} PUBLIC clients render)

# Include client libraries to mag
if (${HAS_NCURSES})
  target_compile_definitions(${PROJECT_NAME} PUBLIC HAS_NCURSES=1)
  target_link_libraries(${PROJECT_NAME} ncurses)
endif()
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})

if (WIN32)
  add_dependencies(${PROJECT_NAME} sdl2_dlls)
endif()

target_link_libraries(${PROJECT_NAME} tracy)

if (NOT WIN32)
  target_link_libraries(${PROJECT_NAME} pthread dl)
endif()

# Run GNU Global if it is available.
if (WIN32)
  add_custom_target(update_global
                    COMMAND ${CMAKE_SOURCE_DIR}/update-global.bat
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
  add_custom_target(update_global
                    COMMAND ${CMAKE_SOURCE_DIR}/update-global.sh
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
add_dependencies(${PROJECT_NAME} update_global)

# Include icon / logo
if (WIN32)
  # Build logo.png into logo.ico
  add_custom_target(icon DEPENDS ${CMAKE_SOURCE_DIR}/res/logo.ico)
  add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/res/logo.ico
                     COMMAND magick convert ${CMAKE_SOURCE_DIR}/res/logo.png -define icon:auto-resize=256,128,96,64,48,32,16 ${CMAKE_SOURCE_DIR}/res/logo.ico
                     DEPENDS ${CMAKE_SOURCE_DIR}/res/logo.png)
else()
  add_custom_target(icon DEPENDS ${CMAKE_BINARY_DIR}/logo.png)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/logo.png
                     COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/logo.png ${CMAKE_BINARY_DIR}
                     DEPENDS ${CMAKE_SOURCE_DIR}/res/logo.png)
endif()
add_dependencies(${PROJECT_NAME} icon)

if (WIN32)
  # Copy diff and clang_format binaries and dependencies
  add_custom_target(copy_resources
      DEPENDS ${CMAKE_BINARY_DIR}/diff.exe
              ${CMAKE_BINARY_DIR}/diff_COPYING
              ${CMAKE_BINARY_DIR}/libiconv2.dll
              ${CMAKE_BINARY_DIR}/libintl3.dll
              ${CMAKE_BINARY_DIR}/clang-format.exe)

  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/diff.exe
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/diff.exe ${CMAKE_BINARY_DIR}
      DEPENDS ${CMAKE_SOURCE_DIR}/res/diff.exe)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/diff_COPYING
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/diff_COPYING ${CMAKE_BINARY_DIR}
      DEPENDS ${CMAKE_SOURCE_DIR}/res/diff_COPYING)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libiconv2.dll
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/libiconv2.dll ${CMAKE_BINARY_DIR}
      DEPENDS ${CMAKE_SOURCE_DIR}/res/libiconv2.dll)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libintl3.dll
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/libintl3.dll ${CMAKE_BINARY_DIR}
      DEPENDS ${CMAKE_SOURCE_DIR}/res/libintl3.dll)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/clang-format.exe
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/res/clang-format.exe ${CMAKE_BINARY_DIR}
      DEPENDS ${CMAKE_SOURCE_DIR}/res/clang-format.exe)
else()
  # diff is assumed to be installed on linux
  add_custom_target(copy_resources)
endif()
add_dependencies(${PROJECT_NAME} copy_resources)

# If tracy is enabled, disable building tests.
if (TRACY_ENABLE)
  add_definitions(-DTRACY_ENABLE)
else()
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
  if (NOT WIN32)
    target_link_libraries(${TEST_EXECUTABLE} pthread dl)
  endif()
endif()

add_library(tracy tracy/TracyClient.cpp)

if(MSVC)
  add_definitions(-D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8 /wd4100 /wd26495 /wd26812")
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror=switch -Wno-unused-parameter")
endif()
