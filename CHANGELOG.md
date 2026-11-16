# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] - 2026-11-16

### Fixed

- The export check `argx.exports` works with MinGW-w64: it reads the export table of the DLL with the `objdump` of the toolchain instead of `dumpbin`, which only MSVC has. MinGW-w64 is now built and tested in CI. ([#2](https://github.com/vabelorg/argx/issues/2))

## [0.1.0] - 2026-11-15

### Added

- Command-line argument parser for C99, with no dependencies beyond the C standard library.
- Options, positionals and nested subcommands.
- Typed values, including user-defined types.
- Help and usage text generated from the schema.
- Builds on Linux, macOS and Windows; usable through CMake or pkg-config.

[Unreleased]: https://github.com/vabelorg/argx/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/vabelorg/argx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/vabelorg/argx/releases/tag/v0.1.0
