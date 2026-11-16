# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-11-16

### Added

- Subcommand summaries for the parent's `Commands:` list in the help, separate from the description (`argx_command_set_summary()`). ([#4](https://github.com/vabelorg/argx/issues/4))
- Terminal flags such as `--help` and `--version` end the parse, so `pack create --help` no longer fails on missing required arguments (`argx_option_set_terminal()`). ([#8](https://github.com/vabelorg/argx/issues/8))
- Global options, defined once and accepted before or after any subcommand (`argx_option_set_global()`). ([#9](https://github.com/vabelorg/argx/issues/9))
- List options that collect repeated values, such as `-I a -I b`, optionally split by a separator, such as `--keys a,b` (`argx_command_add_list()`). ([#3](https://github.com/vabelorg/argx/issues/3))
- A measure function, so that the help aligns and wraps non-ASCII text by its display width (`argx_help_options_set_measure()`). ([#15](https://github.com/vabelorg/argx/issues/15))

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

[Unreleased]: https://github.com/vabelorg/argx/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/vabelorg/argx/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/vabelorg/argx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/vabelorg/argx/releases/tag/v0.1.0
