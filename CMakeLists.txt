cmake_minimum_required(VERSION 3.20)
project(dmxplayer VERSION 0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#include_directories("")
add_compile_options(-Wall -Wextra)

set (dmxplayer_SRC
  dmxplayer.cpp
  commandlineparser.cpp
  main.cpp
  oscreceiver/oscreceiver.cpp
  cuemslogger/cuemslogger.cpp
  mtcreceiver/mtcreceiver.cpp
)

set (dmxplayer_LIBS
  -lrtmidi -lpthread -lxerces-c -lstdc++fs -lola -lolacommon -loscpack
)

add_executable(dmxplayer ${dmxplayer_SRC})
target_link_libraries(dmxplayer ${dmxplayer_LIBS})
#target_link_options(dmxplayer PRIVATE -fsanitize=address)
