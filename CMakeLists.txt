cmake_minimum_required(VERSION 3.0)
project(SDL_kitchensink C)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(KIT_VERSION_MAJOR "0")
set(KIT_VERSION_MINOR "0")
set(KIT_VERSION_PATCH "6")
set(KIT_VERSION ${KIT_VERSION_MAJOR}.${KIT_VERSION_MINOR}.${KIT_VERSION_PATCH})
add_definitions(
    -DKIT_VERSION_MAJOR=${KIT_VERSION_MAJOR}
    -DKIT_VERSION_MINOR=${KIT_VERSION_MINOR}
    -DKIT_VERSION_PATCH=${KIT_VERSION_PATCH}
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=c99")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -pedantic -Werror -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -ggdb -O2 -fno-omit-frame-pointer -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -Os -DNDEBUG")

option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_TESTS "Build unittests" OFF)

find_package(SDL2)
find_package(ass)
find_package(ffmpeg COMPONENTS avcodec avformat avutil swscale swresample)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

include_directories(
    include/
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${ASS_INCLUDE_DIRS}
)

FILE(GLOB SOURCES "src/*.c")
FILE(GLOB HEADERS "include/kitchensink/*.h")

add_library(SDL_kitchensink SHARED ${SOURCES})
add_library(SDL_kitchensink_static STATIC ${SOURCES})

set_target_properties(SDL_kitchensink PROPERTIES VERSION ${KIT_VERSION})
set_target_properties(SDL_kitchensink PROPERTIES SOVERSION ${KIT_VERSION_MAJOR})
set_target_properties(SDL_kitchensink PROPERTIES DEBUG_POSTFIX "d")
set_target_properties(SDL_kitchensink_static PROPERTIES DEBUG_POSTFIX "d")
target_compile_definitions(SDL_kitchensink PRIVATE "KIT_DLL;KIT_DLL_EXPORTS")
target_compile_options(SDL_kitchensink PRIVATE "-fvisibility=hidden")

target_link_libraries(SDL_kitchensink
    ${SDL2_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    ${ASS_LIBRARIES}
)

if(BUILD_EXAMPLES)
    add_executable(exampleaudio examples/example_audio.c)
    add_executable(examplevideo examples/example_video.c)

    if(MINGW)
        target_link_libraries(exampleaudio mingw32)
        target_link_libraries(examplevideo mingw32)
    endif()

    target_link_libraries(exampleaudio
        SDL_kitchensink_static
        ${SDL2_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${ASS_LIBRARIES}
    )
    target_link_libraries(examplevideo
        SDL_kitchensink_static
        ${SDL2_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${ASS_LIBRARIES}
    )
endif()

# Installation
include(GNUInstallDirs)

INSTALL(FILES ${HEADERS} DESTINATION include/kitchensink/)
INSTALL(TARGETS SDL_kitchensink SDL_kitchensink_static
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
