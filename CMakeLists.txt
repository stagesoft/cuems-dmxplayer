# SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
cmake_minimum_required(VERSION 3.20)
project(dmxplayer VERSION 0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#include_directories("")
add_compile_options(-Wall -Wextra)

# Unit tests (off by default: the package build is unchanged)
option(BUILD_TESTS "Build the unit tests (tests/)" OFF)
if(BUILD_TESTS)
  # the tests drive mtcreceiver's head-authoritative flag
  set(MTCRECV_TESTING ON CACHE BOOL "Expose mtcreceiver test-only helpers" FORCE)
endif()

# Subdirectories
add_subdirectory(oscreceiver)
add_subdirectory(mtcreceiver)
add_subdirectory(cuemslogger)

set (cuems-dmxplayer_SRC
  dmxplayer.cpp
  commandlineparser.cpp
  main.cpp
)

set (cuems-dmxplayer_LIBS
  cuemslogger
  mtcreceiver
  oscreceiver
  -lrtmidi -lpthread -lxerces-c -lstdc++fs -lola -lolacommon
)

add_executable(cuems-dmxplayer ${cuems-dmxplayer_SRC})
target_link_libraries(cuems-dmxplayer ${cuems-dmxplayer_LIBS})
target_compile_definitions(mtcreceiver PUBLIC HAVE_CUEMS_LOGGER)
#target_link_options(cuems-dmxplayer PRIVATE -fsanitize=address)

install(TARGETS cuems-dmxplayer
        RUNTIME DESTINATION bin
)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
