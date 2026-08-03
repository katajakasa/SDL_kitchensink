find_package(PkgConfig REQUIRED)
pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

# Media fixtures are committed to git and read straight from the source tree;
# they are (re)generated with `make -C test-data`, never by this build. See
# test-data/Makefile.
set(KIT_TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/test-data/media")

# Unlike the built-in `test` target, `check` builds the test executables
# before running ctest.
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)

# Registers tests/<tier>/test_<name>.c as executable test_<name> and
# CTest entry "<tier>/<name>" labeled with the tier (plus any extra
# labels given after the name), so groups can be run selectively with
# e.g. `ctest -L unit` or excluded with `ctest -LE stress`.
function(kit_add_test TIER NAME)
    set(TARGET test_${NAME})
    add_executable(${TARGET} ${TIER}/test_${NAME}.c)
    set_property(TARGET ${TARGET} PROPERTY C_STANDARD 99)
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
    target_compile_definitions(${TARGET} PRIVATE "KIT_TEST_DATA_DIR=\"${KIT_TEST_DATA_DIR}\"")
    if (USE_ASAN)
        # Linking via the -fsanitize flag makes the compiler driver put its own
        # matching sanitizer runtime first in the link order (works for both
        # gcc and clang, unlike naming the gcc runtime library directly).
        target_compile_options(${TARGET} PRIVATE "-fsanitize=address")
        target_link_libraries(${TARGET} "-fsanitize=address")
    endif ()
    if (USE_TSAN)
        # See the USE_ASAN comment above.
        target_compile_options(${TARGET} PRIVATE "-fsanitize=thread")
        target_link_libraries(${TARGET} "-fsanitize=thread")
    endif ()
    target_link_libraries(${TARGET} SDL_kitchensink_static ${LIBRARIES} PkgConfig::CMOCKA)
    add_dependencies(check ${TARGET})
    add_test(NAME ${TIER}/${NAME} COMMAND ${TARGET})
    set(LABELS ${TIER} ${ARGN})
    set_tests_properties(
        ${TIER}/${NAME} PROPERTIES
        LABELS "${LABELS}"
        TIMEOUT 120
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
    )
endfunction()

kit_add_test(unit helpers)
kit_add_test(unit timer)
kit_add_test(unit timer_mt)
kit_add_test(unit packetbuffer)
kit_add_test(unit packetbuffer_mt)
kit_add_test(unit audioutils)
kit_add_test(unit videoutils)
kit_add_test(unit atlas)
kit_add_test(unit packettag)
kit_add_test(unit subtitlepacket)
kit_add_test(unit decoder)
kit_add_test(unit decoderthreads)

kit_add_test(api lib)
kit_add_test(api error)
kit_add_test(api error_mt)
kit_add_test(api format)
kit_add_test(api utils)
kit_add_test(api source)
kit_add_test(api source_io)

kit_add_test(decoder demuxer)
kit_add_test(decoder player)
kit_add_test(decoder audio_formats)
kit_add_test(decoder video_formats)
kit_add_test(decoder texture_formats)
kit_add_test(decoder subtitle_render)
kit_add_test(decoder playback_bounds)
kit_add_test(decoder stream_switch)
kit_add_test(decoder player_stress stress)
kit_add_test(decoder broken_input)

if (KIT_FAULT_INJECTION)
    # Registry semantics of the fault-injection framework itself.
    kit_add_test(unit faultinject)
    # Constructor-unwind sweeps via the "alloc" fault point.
    kit_add_test(decoder alloc_unwind)
    # Deterministic demuxer I/O-failure tests via the "demux_read"/"demux_seek"
    # fault points.
    kit_add_test(decoder fault_io)
    # Deterministic decoder-error tests via the "decode_send"/"decode_receive"
    # fault points.
    kit_add_test(decoder fault_decode)
    # Resource-creation fault tests via the "sdl_mutex"/"swr_init"/"sws_init"
    # fault points.
    kit_add_test(decoder fault_resources)
endif ()
