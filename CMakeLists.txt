cmake_minimum_required(VERSION 3.10)
project(SDL_kitchensink C)
include(GNUInstallDirs)
include(CheckLibraryExists)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(KIT_VERSION_MAJOR "3")
set(KIT_VERSION_MINOR "0")
set(KIT_VERSION_PATCH "0")
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
option(USE_TSAN "Use ThreadSanitizer" OFF)
option(USE_TIDY "Use clang-tidy" OFF)
option(USE_FORMAT "Use clang-format for formatting" OFF)
option(BUILD_SHARED "Build shared library" ON)
option(BUILD_STATIC "Build static library" ON)
option(BUILD_TESTS "Build unit tests" OFF)
option(KIT_FAULT_INJECTION "Compile in the debug fault-injection registry (tests only)" OFF)

if (USE_ASAN AND USE_TSAN)
    message(FATAL_ERROR "USE_ASAN and USE_TSAN are mutually exclusive")
endif ()

if (NOT BUILD_SHARED AND NOT BUILD_STATIC)
    message(FATAL_ERROR "Nothing to build, set BUILD_SHARED and/or BUILD_STATIC.")
endif ()

if (BUILD_SHARED)
    message(STATUS "Building shared libraries")
else ()
    message(STATUS "NOT building shared libraries")
endif ()

if (BUILD_STATIC)
    message(STATUS "Building static libraries")
else ()
    message(STATUS "NOT building static libraries")
endif ()

if (USE_ASAN)
    message(STATUS "DEVELOPMENT: AddressSanitizer enabled!")
endif ()

if (USE_TSAN)
    message(STATUS "DEVELOPMENT: ThreadSanitizer enabled!")
endif ()

find_package(SDL3 REQUIRED CONFIG)
find_package(ffmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)

check_library_exists(m pow "" LIBM_EXISTS)

set(LIBRARIES
        SDL3::SDL3
        ${FFMPEG_LIBRARIES}
)
set(INCLUDES
        include/
        ${FFMPEG_INCLUDE_DIRS}
)

if (LIBM_EXISTS)
    list(APPEND LIBRARIES "m")
endif ()

if (USE_DYNAMIC_LIBASS)
    if (WIN32 OR MINGW OR MSYS)
        set(DYNAMIC_LIBASS_NAME "\"libass-9.dll\"")
    else ()
        set(DYNAMIC_LIBASS_NAME "\"libass.so\"")
    endif ()
    add_definitions(-DUSE_DYNAMIC_LIBASS)
    add_definitions(-DDYNAMIC_LIBASS_NAME=${DYNAMIC_LIBASS_NAME})
else ()
    find_package(ass REQUIRED)
    set(LIBRARIES ${LIBRARIES} ${ASS_LIBRARIES})
    set(INCLUDES ${INCLUDES} ${ASS_INCLUDE_DIRS})
endif ()

FILE(GLOB_RECURSE SOURCES "src/*.c")
FILE(GLOB INSTALL_HEADERS "include/kitchensink3/*.h")

# Compiled in only via the KIT_FAULT_INJECTION option below, not unconditionally.
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/internal/kitfaultinject.c")

include_directories(${INCLUDES})

if (KIT_FAULT_INJECTION)
    message(STATUS "DEVELOPMENT: fault-injection registry enabled!")
    list(APPEND SOURCES "src/internal/kitfaultinject.c")
endif ()

set(INSTALL_TARGETS "")

if (BUILD_SHARED)
    add_library(SDL_kitchensink SHARED ${SOURCES})

    if (USE_ASAN)
        target_compile_options(SDL_kitchensink PRIVATE "-fsanitize=address")
        target_link_libraries(SDL_kitchensink "-fsanitize=address")
    endif ()

    if (USE_TSAN)
        target_compile_options(SDL_kitchensink PRIVATE "-fsanitize=thread")
        target_link_libraries(SDL_kitchensink "-fsanitize=thread")
    endif ()

    target_link_libraries(SDL_kitchensink ${LIBRARIES})

    set_target_properties(SDL_kitchensink PROPERTIES OUTPUT_NAME "SDL_kitchensink3")
    set_target_properties(SDL_kitchensink PROPERTIES VERSION ${KIT_VERSION})
    set_target_properties(SDL_kitchensink PROPERTIES SOVERSION ${KIT_VERSION_MAJOR})
    set_target_properties(SDL_kitchensink PROPERTIES DEBUG_POSTFIX "d")

    target_compile_definitions(SDL_kitchensink PRIVATE "KIT_DLL;KIT_DLL_EXPORTS")
    target_compile_options(SDL_kitchensink PRIVATE "-fvisibility=hidden")

    if (KIT_FAULT_INJECTION)
        target_compile_definitions(SDL_kitchensink PRIVATE KIT_FAULT_INJECTION)
    endif ()

    set_property(TARGET SDL_kitchensink PROPERTY C_STANDARD 99)

    set(INSTALL_TARGETS SDL_kitchensink ${INSTALL_TARGETS})
endif ()

if (BUILD_STATIC)
    add_library(SDL_kitchensink_static STATIC ${SOURCES})

    target_link_libraries(SDL_kitchensink_static ${LIBRARIES})

    if (USE_ASAN)
        target_compile_options(SDL_kitchensink_static PRIVATE "-fsanitize=address")
    endif ()

    if (USE_TSAN)
        target_compile_options(SDL_kitchensink_static PRIVATE "-fsanitize=thread")
    endif ()

    set_target_properties(SDL_kitchensink_static PROPERTIES OUTPUT_NAME "SDL_kitchensink3")
    set_target_properties(SDL_kitchensink_static PROPERTIES DEBUG_POSTFIX "d")
    set_property(TARGET SDL_kitchensink_static PROPERTY C_STANDARD 99)

    if (KIT_FAULT_INJECTION)
        target_compile_definitions(SDL_kitchensink_static PUBLIC KIT_FAULT_INJECTION)
    endif ()

    set(INSTALL_TARGETS SDL_kitchensink_static ${INSTALL_TARGETS})
endif ()

if (BUILD_TESTS)
    if (NOT BUILD_STATIC)
        message(FATAL_ERROR "BUILD_TESTS requires BUILD_STATIC=ON")
    endif ()
    enable_testing()
    add_subdirectory(tests)
endif ()

if (USE_TIDY)
    if (BUILD_STATIC)
        set_target_properties(SDL_kitchensink_static PROPERTIES C_CLANG_TIDY "clang-tidy")
    else ()
        set_target_properties(SDL_kitchensink PROPERTIES C_CLANG_TIDY "clang-tidy")
    endif ()
    message(STATUS "Development: clang-tidy enabled")
else ()
    message(STATUS "Development: clang-tidy disabled")
endif ()

if (USE_FORMAT)
    include(cmake/ClangFormat.cmake)
    file(GLOB_RECURSE FORMAT_FILES
            LIST_DIRECTORIES OFF
            CONFIGURE_DEPENDS
            RELATIVE ${CMAKE_SOURCE_DIR}
            "src/*.c"
            "src/*.h"
            "include/*.h"
            "examples/*.c"
            "tests/*.c"
    )
    clangformat_setup(${FORMAT_FILES})
    message(STATUS "Development: clang-format enabled")
else ()
    message(STATUS "Development: clang-format disabled")
endif ()

set(PC_REQUIRES_PRIVATE "sdl3 libavcodec libavformat libavutil libswresample libswscale")
if (NOT USE_DYNAMIC_LIBASS)
    string(APPEND PC_REQUIRES_PRIVATE " libass")
endif ()
set(PC_LIBS_PRIVATE "")
if (LIBM_EXISTS)
    set(PC_LIBS_PRIVATE "-lm")
endif ()
set(PKG_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/SDL_kitchensink3.pc")
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/pkg-config.pc.in"
        ${PKG_CONFIG_FILE}
        @ONLY
)

if (BUILD_EXAMPLES)
    list(APPEND EXAMPLE_TARGETS audio complex simple custom rwops rawdump)

    # The OpenGL cube example needs an OpenGL library; skip it if not available.
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL)
    if (OpenGL_FOUND)
        list(APPEND EXAMPLE_TARGETS glcube)
    else ()
        message(STATUS "OpenGL not found, skipping glcube example")
    endif ()

    # If we are building static, just link all libraries (ffmpeg, sdl, etc.)
    # If building shared, link shared kitchensink + SDL2 (ffmpeg gets pulled by kitchensink)
    if (BUILD_STATIC)
        set(EXAMPLE_LIBRARIES SDL_kitchensink_static ${LIBRARIES})
    else ()
        set(EXAMPLE_LIBRARIES SDL_kitchensink SDL3::SDL3)
    endif ()
    if (USE_ASAN)
        set(EXAMPLE_LIBRARIES "-fsanitize=address" ${EXAMPLE_LIBRARIES})
    endif ()
    if (USE_TSAN)
        set(EXAMPLE_LIBRARIES "-fsanitize=thread" ${EXAMPLE_LIBRARIES})
    endif ()

    foreach (TARGET ${EXAMPLE_TARGETS})
        add_executable(${TARGET} examples/example_${TARGET}.c)
        set_property(TARGET ${TARGET} PROPERTY C_STANDARD 99)
        target_link_libraries(${TARGET} ${EXAMPLE_LIBRARIES})

        if (MINGW)
            # If we are compiling with mingw, remember to link in mingw libs and set console mode
            # This way stdout/stderr are handled correctly.
            target_link_libraries(${TARGET} mingw32)
            set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-mconsole")
        endif ()

        if (USE_ASAN)
            target_compile_options(${TARGET} PRIVATE "-fsanitize=address")
        endif ()
        if (USE_TSAN)
            target_compile_options(${TARGET} PRIVATE "-fsanitize=thread")
        endif ()
    endforeach ()

    if (TARGET glcube)
        target_link_libraries(glcube OpenGL::GL)
    endif ()
endif ()

# documentation target
add_custom_target(docs COMMAND doxygen WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Installation
INSTALL(FILES ${PKG_CONFIG_FILE} DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
INSTALL(FILES ${INSTALL_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kitchensink3)
INSTALL(TARGETS ${INSTALL_TARGETS}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
