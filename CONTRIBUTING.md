# Contributing

Thanks for your interest in SDL_kitchensink! This page covers how development
is organized; the details live in the developer docs:

* [docs/COMPILING.md](docs/COMPILING.md) -- dependencies, building, CMake
  options, developer builds
* [docs/TESTING.md](docs/TESTING.md) -- test suite, sanitizers, fault
  injection, test media
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) -- project structure and
  internals
* [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md) -- code style and conventions

## Branches and versions

| Version | SDL   | Branch     | Accepts                  |
|---------|-------|------------|--------------------------|
| 3.x.x   | 3.x.x | master     | Bugfixes and new features |
| 2.x.x   | 2.x.x | release/v2 | Bugfixes and new features |
| 1.x.x   | 2.x.x | release/v1 | Small bugfixes only       |
| 0.x.x   | 2.x.x | release/v0 | Nothing (end of life)     |

Active development happens on **master** (the v3 series, SDL3-based) -- target
your pull requests there unless the change is specific to an older series.
v2 is the last series to support SDL2; fixes applicable to both series are
welcome on both branches, but land them on master first.

## Pull requests

Before opening a PR:

1. Build and test under both sanitizers -- these must pass:

   ```sh
   make test-asan
   make test-tsan
   ```

   These targets also enable clang-tidy, so lint is checked in the same
   step, and they make the `clangformat` reformatting target available (see
   [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md)). New code should come with
   tests; see [docs/TESTING.md](docs/TESTING.md) for how the suite is
   organized.

2. Keep the public API stable. Changes to `include/kitchensink3/*.h` that
   break compatibility are major-version material and need discussion first --
   open an issue before investing work in one.

CI runs the compile matrix (gcc 13-15, clang 18-20) plus release, ASan, TSan
and dynamic-libass test configurations on every pull request; the same suite
you ran locally, so green locally usually means green CI.

## AI-assisted contributions

AI-based commits are accepted, but the submitter MUST understand the contents
of every change they submit, and every change MUST be finally reviewed by a
human before it is merged. Do not submit generated code you cannot explain.
The submitter is also responsible for making sure the submitted code is
compatible with the project's MIT license -- do not submit generated code
that reproduces incompatibly licensed material.

## Reporting issues

Security issues: see [SECURITY.md](SECURITY.md). Everything else: GitHub
issues, ideally with the media file (or a description of it), your platform,
and library versions.
