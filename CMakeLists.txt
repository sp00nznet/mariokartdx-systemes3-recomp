cmake_minimum_required(VERSION 3.20)
project(mariokartdx VERSION 0.1.0 LANGUAGES C)

# Mario Kart Arcade GP DX (Namco / Nintendo, 2013), statically recompiled.
#
# The game target only exists once you have lifted an executable, because the
# runtime needs three headers generated from it. Without them this still
# configures and builds the toolkit's own self-checks, so the repo is testable
# out of the box.
#
#   py -3.11 -m tools scan   MK_AGP3_FINAL.exe catalog.json     (slow, once)
#   py -3.11 -m tools recomp MK_AGP3_FINAL.exe catalog.json ^
#                            ..\games\mariokartdx\generated
#   cmake -S . -B build -A Win32
#   cmake --build build --config Release

# Before the subdirectory, or ctest at this level finds none of the toolkit's
# tests - enable_testing() only registers tests at and below where it is called.
enable_testing()

add_subdirectory(systemes3recomp)

set(GEN "${CMAKE_CURRENT_SOURCE_DIR}/games/mariokartdx/generated")

file(GLOB LIFTED "${GEN}/recomp_funcs_*.c")
if(LIFTED AND EXISTS "${GEN}/recomp_imports.h")
    add_executable(mariokartdx
        games/mariokartdx/src/host.c
        ${LIFTED}
    )
    es3_runtime(mariokartdx "${GEN}")
    list(LENGTH LIFTED N)
    message(STATUS "mariokartdx: building against ${N} lifted translation units in ${GEN}")
else()
    message(STATUS
        "mariokartdx: no lifted code in ${GEN} yet, building the toolkit "
        "self-checks only. See the README.")
endif()
