cmake_minimum_required(VERSION 2.8)
project(SDL_kitchensink C)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "0")
set(VERSION_PATCH "1")
add_definitions(
	-DKIT_VERSION_MAJOR=${VERSION_MAJOR}
	-DKIT_VERSION_MINOR=${VERSION_MINOR}
	-DKIT_VERSION_PATCH=${VERSION_PATCH}
)

set(CMAKE_C_FLAGS "-Wall -std=c99")
set(CMAKE_C_FLAGS_DEBUG "-ggdb -Werror -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -O2 -fno-omit-frame-pointer -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")

option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_TESTS "Build unittests" OFF)

find_package(SDL2)
find_package(ffmpeg COMPONENTS avcodec avformat avutil swscale swresample)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

include_directories(
	include/
	${SDL2_INCLUDE_DIRS}
	${FFMPEG_INCLUDE_DIRS}
)

set(SOURCES 
	src/kitchensink.c
	src/kiterror.c
	src/kitplayer.c
	src/kitsource.c
)

add_library(SDL_kitchensink SHARED ${SOURCES})
add_library(SDL_kitchensink_static STATIC ${SOURCES})

set_target_properties(SDL_kitchensink PROPERTIES DEBUG_POSTFIX "d")
set_target_properties(SDL_kitchensink_static PROPERTIES DEBUG_POSTFIX "d")

target_link_libraries(SDL_kitchensink
	${SDL2_LIBRARIES}
	${FFMPEG_LIBRARIES}
)

if(BUILD_EXAMPLES)
	add_executable(exampleplay examples/example_play.c)
	if(MINGW)
		target_link_libraries(exampleplay mingw32)
	endif()
	target_link_libraries(exampleplay
		SDL_kitchensink_static
		${SDL2_LIBRARIES}
		${FFMPEG_LIBRARIES}
	)
endif()

# Installation
FILE(GLOB H_FILES "include/kitchensink/*.h")
INSTALL(FILES ${H_FILES} DESTINATION include/kitchensink/)
INSTALL(TARGETS SDL_kitchensink SDL_kitchensink_static
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
