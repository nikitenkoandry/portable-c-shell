# Changelog

All notable changes to Portable C Shell are documented in this file.

## [0.1.0] - 2026-09-23

Initial public release.

### Added

- Heap-free C99 shell core with caller-owned storage.
- Static nested commands and independently registered command sets.
- Argument-count validation with default and extended 32/64 profiles.
- Quoted and escaped tokenization.
- Fixed-depth history with Up/Down navigation and draft restoration.
- Cursor-aware Tab completion and dynamic argument providers.
- ANSI line editing with cursor, deletion, Home/End, and Ctrl shortcuts.
- Sensitive input masking, redacted/no-history commands, and secure clearing.
- Bounded partial-write transport contract with backpressure reporting.
- FreeRTOS StreamBuffer, bare-metal polling, raw TCP, Windows, and POSIX ports.
- Host terminal, TCP loopback, FreeRTOS UART, and bare-metal examples.
- Strict CMake builds, 26 CTest cases, coverage thresholds, static analysis,
  sanitizer configuration, deterministic fuzz streams, and GitHub Actions CI.
- Doxygen API reference plus porting, command, FreeRTOS, TCP, and testing guides.

[0.1.0]: https://github.com/nikitenkoandry/portable-c-shell/releases/tag/v0.1.0
