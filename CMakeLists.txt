cmake_minimum_required(VERSION 3.20)
project(dmxplayer VERSION 0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#include_directories("")
add_compile_options(-Wall -Wextra)

# Subdirectories
add_subdirectory(oscreceiver)
add_subdirectory(mtcreceiver)
add_subdirectory(cuemslogger)

set (dmxplayer_SRC
  dmxplayer.cpp
  commandlineparser.cpp
  main.cpp
)

set (dmxplayer_LIBS
  cuemslogger
  mtcreceiver
  oscreceiver
  -lrtmidi -lpthread -lxerces-c -lstdc++fs -lola -lolacommon
)

add_executable(dmxplayer ${dmxplayer_SRC})
target_link_libraries(dmxplayer ${dmxplayer_LIBS})
#target_link_options(dmxplayer PRIVATE -fsanitize=address)
