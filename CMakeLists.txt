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
