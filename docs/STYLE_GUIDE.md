# Style guide

The codebase is C99, compiled with `-Wall` (and `-Werror` in debug builds).
Source files are plain ASCII -- no Unicode in identifiers, comments or string
literals. Most style questions are settled by tooling; the rest are
conventions listed here -- when in doubt, match the surrounding code.

## 1. Tooling

Formatting is owned by **clang-format** (config in `.clang-format`: LLVM
base, 119-column limit, 4-space indent, no space before parentheses). Don't
format by hand or argue with the tool. Configuring with `-DUSE_FORMAT=1`
(done by the `build-asan`/`build-tsan` Makefile targets) adds a build target
that reformats all sources in place:

```sh
ninja -C build clangformat
```

Static analysis is owned by **clang-tidy** (config in `.clang-tidy`:
clang-analyzer and performance checks, warnings are errors). With
`-DUSE_TIDY=1` it runs on every library source file as part of the build, so
a sanitizer build from the Makefile fails on new findings. CI builds with it
enabled as well.

## 2. Naming

* Public functions and internal functions alike use the `Kit_` prefix with
  PascalCase: `Kit_CreatePlayer()`, `Kit_RunDemuxer()`. Static file-local
  helpers also follow this pattern.
* Types are `Kit_` PascalCase typedefs: `Kit_Player`, `Kit_PacketBuffer`.
* Enum values and macros are `KIT_` upper snake case: `KIT_PLAYING`,
  `KIT_DEC_INPUT_OK`.
* Files are lower case without separators, `kit` prefixed: `kitplayer.c`,
  `kitdemuxerthread.h`. Header guards are the filename in caps: `KITPLAYER_H`.
* Struct members and local variables are lower snake case.

## 3. API visibility

Public functions are declared in `include/kitchensink3/*.h` and marked
`KIT_API`; internal ones live under `include/kitchensink3/internal/` and are
marked `KIT_LOCAL` (hidden in shared builds). See `kitconfig.h`. Anything not
meant to be part of the stable API must not appear in a public header.

## 4. Error handling

Errors are reported with `Kit_SetError()` (printf-style, thread-local
storage) at the point of failure, and functions return NULL / false / an
error code per their documented contract.

Multi-step constructors use the numbered-label unwind idiom: each successful
acquisition moves the failure target one label forward, and the labels tear
down in reverse order, falling through each other:

```c
if((foo = Kit_CreateFoo()) == NULL)
    goto exit_0;
if((bar = Kit_CreateBar(foo)) == NULL)
    goto exit_1;
if((baz = Kit_CreateBaz(bar)) == NULL)
    goto exit_2;
return thing;

exit_2:
    Kit_CloseBar(&bar);
exit_1:
    Kit_CloseFoo(&foo);
exit_0:
    return NULL;
```

Destructors take a pointer-to-pointer (`Kit_CloseFoo(Foo **foo)`), free the
object, NULL out the caller's pointer, and are no-ops when passed NULL or an
already-NULL pointer -- so cleanup paths never need NULL checks.

The fault-injection fail points (see [TESTING.md](TESTING.md)) exist to keep
these unwind paths tested; when adding a new fallible step to a constructor,
make sure a fault sweep can reach it.

## 5. Comments and documentation

* Every header carries a Doxygen `@file` block, and every declaration in a
  header gets a Doxygen comment: `@brief` plus `@param`/`@return` where
  applicable. Document the contract -- blocking behavior, ownership, NULL
  handling, thread-safety -- not the implementation.
* Comments in `.c` files are for when the code cannot express itself:
  why something is done this way, what invariant is being protected. Keep
  them short and self-sufficient.
* Don't reference issue numbers, PRs or review discussions in code comments;
  the comment must stand on its own for a reader without that context.

## 6. Threading conventions

New cross-thread communication should go through `Kit_PacketBuffer` or SDL
atomics/mutexes following the existing patterns (see
[ARCHITECTURE.md](ARCHITECTURE.md)). All new code must pass the ThreadSanitizer suite
(`make test-tsan`).
