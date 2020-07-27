cmake_minimum_required(VERSION 3.7)
project(SDL_kitchensink C)
include(GNUInstallDirs)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(KIT_VERSION_MAJOR "1")
set(KIT_VERSION_MINOR "0")
set(KIT_VERSION_PATCH "9")
set(KIT_VERSION ${KIT_VERSION_MAJOR}.${KIT_VERSION_MINOR}.${KIT_VERSION_PATCH})
add_definitions(
    -DKIT_VERSION_MAJOR=${KIT_VERSION_MAJOR}
    -DKIT_VERSION_MINOR=${KIT_VERSION_MINOR}
    -DKIT_VERSION_PATCH=${KIT_VERSION_PATCH}
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -Werror -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -ggdb -O2 -fno-omit-frame-pointer -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -Os -DNDEBUG")

option(BUILD_EXAMPLES "Build examples" OFF)
option(USE_DYNAMIC_LIBASS "Use dynamically loaded libass" OFF)
option(USE_ASAN "Use AddressSanitizer" OFF)
option(BUILD_SHARED "Build shared library" ON)
option(BUILD_STATIC "Build static library" ON)

if(NOT BUILD_SHARED AND NOT BUILD_STATIC)
    message(FATAL_ERROR "Nothing to build, set BUILD_SHARED and/or BUILD_STATIC.")
endif()

if(BUILD_SHARED)
    message(STATUS "Building shared libraries")
else()
    message(STATUS "NOT building shared libraries")
endif()

if(BUILD_STATIC)
    message(STATUS "Building static libraries")
else()
    message(STATUS "NOT building static libraries")
endif()

if(USE_ASAN)
    message(STATUS "DEVELOPMENT: AddressSanitizer enabled!")
endif()

find_package(SDL2 REQUIRED)
find_package(ffmpeg COMPONENTS avcodec avformat avutil swscale swresample)

set(LIBRARIES
    ${SDL2_LIBRARIES}
    ${FFMPEG_LIBRARIES}
)
set(INCLUDES
    include/
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

if(USE_DYNAMIC_LIBASS)
    if(WIN32 OR MINGW OR MSYS)
        set(DYNAMIC_LIBASS_NAME "\"libass-9.dll\"")
    else()
        set(DYNAMIC_LIBASS_NAME "\"libass.so\"")
    endif()
    add_definitions(-DUSE_DYNAMIC_LIBASS)
    add_definitions(-DDYNAMIC_LIBASS_NAME=${DYNAMIC_LIBASS_NAME})
else()
    find_package(ass)
    set(LIBRARIES ${LIBRARIES} ${ASS_LIBRARIES})
    set(INCLUDES ${INCLUDES} ${ASS_INCLUDE_DIRS})
endif()

FILE(GLOB_RECURSE SOURCES "src/*.c")
FILE(GLOB INSTALL_HEADERS "include/kitchensink/*.h")

include_directories(${INCLUDES})

set(INSTALL_TARGETS "")

if(BUILD_SHARED)
    add_library(SDL_kitchensink SHARED ${SOURCES})

    if(USE_ASAN)
        target_compile_options(SDL_kitchensink PRIVATE "-fsanitize=address")
        target_link_libraries(SDL_kitchensink asan)
    endif()

    target_link_libraries(SDL_kitchensink ${LIBRARIES})

    set_target_properties(SDL_kitchensink PROPERTIES VERSION ${KIT_VERSION})
    set_target_properties(SDL_kitchensink PROPERTIES SOVERSION ${KIT_VERSION_MAJOR})
    set_target_properties(SDL_kitchensink PROPERTIES DEBUG_POSTFIX "d")

    target_compile_definitions(SDL_kitchensink PRIVATE "KIT_DLL;KIT_DLL_EXPORTS")
    target_compile_options(SDL_kitchensink PRIVATE "-fvisibility=hidden")

    set_property(TARGET SDL_kitchensink PROPERTY C_STANDARD 99)

    set(INSTALL_TARGETS SDL_kitchensink ${INSTALL_TARGETS})
endif()

if(BUILD_STATIC)
    add_library(SDL_kitchensink_static STATIC ${SOURCES})

    if(USE_ASAN)
        target_compile_options(SDL_kitchensink_static PRIVATE "-fsanitize=address")
    endif()

    set_target_properties(SDL_kitchensink_static PROPERTIES DEBUG_POSTFIX "d")
    set_property(TARGET SDL_kitchensink_static PROPERTY C_STANDARD 99)

    set(INSTALL_TARGETS SDL_kitchensink_static ${INSTALL_TARGETS})
endif()

set(PKG_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/SDL_kitchensink.pc")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/pkg-config.pc.in"
    ${PKG_CONFIG_FILE}
    @ONLY
)

if(BUILD_EXAMPLES)
    add_executable(audio examples/example_audio.c)
    add_executable(complex examples/example_complex.c)
    add_executable(simple examples/example_simple.c)
    add_executable(custom examples/example_custom.c)
    add_executable(rwops examples/example_rwops.c)

    if(MINGW)
        target_link_libraries(audio mingw32)
        target_link_libraries(complex mingw32)
        target_link_libraries(simple mingw32)
        target_link_libraries(custom mingw32)
        target_link_libraries(rwops mingw32)
    endif()

    set_property(TARGET audio PROPERTY C_STANDARD 99)
    set_property(TARGET complex PROPERTY C_STANDARD 99)
    set_property(TARGET simple PROPERTY C_STANDARD 99)
    set_property(TARGET custom PROPERTY C_STANDARD 99)
    set_property(TARGET rwops PROPERTY C_STANDARD 99)

    # If we are building static, just link all libraries (ffmpeg, sdl, etc.)
    # If building shared, link shared kitchensink + SDL2 (ffmpeg gets pulled by kitchensink)
    if(BUILD_STATIC)
        set(EXAMPLE_LIBRARIES SDL_kitchensink_static ${LIBRARIES})
    else()
        set(EXAMPLE_LIBRARIES SDL_kitchensink ${SDL2_LIBRARIES})
    endif()
    if(USE_ASAN)
        set(EXAMPLE_LIBRARIES asan ${EXAMPLE_LIBRARIES})
    endif()

    target_link_libraries(audio ${EXAMPLE_LIBRARIES})
    target_link_libraries(complex ${EXAMPLE_LIBRARIES})
    target_link_libraries(simple ${EXAMPLE_LIBRARIES})
    target_link_libraries(custom ${EXAMPLE_LIBRARIES})
    target_link_libraries(rwops ${EXAMPLE_LIBRARIES})

    if(USE_ASAN)
        target_compile_options(audio PRIVATE "-fsanitize=address")
        target_compile_options(complex PRIVATE "-fsanitize=address")
        target_compile_options(simple PRIVATE "-fsanitize=address")
        target_compile_options(custom PRIVATE "-fsanitize=address")
        target_compile_options(rwops PRIVATE "-fsanitize=address")
    endif()
endif()

# documentation target
add_custom_target(docs COMMAND doxygen WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Installation
INSTALL(FILES ${PKG_CONFIG_FILE} DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
INSTALL(FILES ${INSTALL_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kitchensink)
INSTALL(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
