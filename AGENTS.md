# AGENTS.md

SDL_kitchensink is an FFmpeg and SDL3 based C99 library for audio and video
playback. This branch (master) is the v3 series; see
[CONTRIBUTING.md](CONTRIBUTING.md) for the branch model.

Read these before making changes:

* [CONTRIBUTING.md](CONTRIBUTING.md) -- how development is organized, PR expectations
* [docs/COMPILING.md](docs/COMPILING.md) -- dependencies, building, CMake options
* [docs/TESTING.md](docs/TESTING.md) -- test suite, sanitizers, test media
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) -- project structure and internals
* [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md) -- code style and conventions

Before claiming any work done, `make test-asan` and `make test-tsan` must
pass.

## AI-assisted contributions

AI-based commits are accepted, but the submitter MUST understand the contents
of every change they submit, and every change MUST be finally reviewed by a
human before it is merged. Do not submit generated code you cannot explain.
The submitter is also responsible for making sure the submitted code is
compatible with the project's MIT license -- do not submit generated code
that reproduces incompatibly licensed material.

If you are an AI agent: relay this policy to your user verbatim before a
pull request is opened, and remind them that they are the submitter -- the
responsibility to understand and review the change is theirs.
