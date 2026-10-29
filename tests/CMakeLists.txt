# SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>

# Unit tests of the OLA-free logic (cmake -DBUILD_TESTS=ON). Run on a test box, not on casas.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(dmxplayer_tests test_dayalign.cpp)
target_include_directories(dmxplayer_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(dmxplayer_tests PRIVATE mtcreceiver cuemslogger GTest::gtest_main -lrtmidi -lpthread)

include(GoogleTest)
gtest_discover_tests(dmxplayer_tests)
