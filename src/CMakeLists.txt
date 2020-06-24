cmake_minimum_required(VERSION 3.10)

# Project name and version
project(cuems_dmxplayer VERSION 0.0)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Subdirectories
add_subdirectory(oscreceiver)
add_subdirectory(mtcreceiver)
add_subdirectory(cuemslogger)

# Executable
add_executable(dmxplayer-cuems main.cpp dmxplayer.cpp dmxcue_v0.cpp dmxcue_v1.cpp commandlineparser.cpp)

# Configure file
configure_file(cuems_dmxplayerConfig.h.in cuems_dmxplayerConfig.h)

# Link libraries
target_link_libraries(dmxplayer-cuems PUBLIC cuemslogger mtcreceiver oscreceiver)
target_link_libraries(dmxplayer-cuems PUBLIC rtmidi pthread stdc++fs xerces-c ola olacommon)

# Include dirs
target_include_directories(dmxplayer-cuems PUBLIC
                            "${PROJECT_SOURCE_DIR}"
                            "${PROJECT_BINARY_DIR}"
                            "${PROJECT_SOURCE_DIR}/mtcreceiver"
                            "${PROJECT_SOURCE_DIR}/oscreceiver"
                            "${PROJECT_SOURCE_DIR}/cuemslogger"
                        )
# Install rules
install(TARGETS dmxplayer-cuems DESTINATION bin)
