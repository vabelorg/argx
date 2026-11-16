# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `argx_command_set_summary()` and `argx_command_summary()`: a subcommand's summary for its parent's `Commands:` list in the help, separate from the description that the subcommand's own help shows. Without a summary, the list shows the first line of the description, as before. ([#4](https://github.com/vabelorg/argx/issues/4))
- `argx_option_set_terminal()` and `argx_option_is_terminal()`: a terminal flag, such as `--help` or `--version`, ends the parse when it is read. The parse succeeds without reading the arguments after the flag, checking what is required or applying default values, so `pack --help` and `pack create --help` are no longer parse errors when they leave out what is required. `argx_result_terminal_option()` returns the flag that ended the parse. ([#8](https://github.com/vabelorg/argx/issues/8))
- `argx_option_set_global()` and `argx_option_is_global()`: a global option is known on its command and on every descendant of it, so `--help`, `--verbose` or `--config` is defined once and can be given before or after the name of a subcommand. It stays one option, read with its one handle: `pack -v create -vv` counts 3. The help of a descendant lists the global options of its ancestors under a new `Global options:` list, and its usage line shows `[OPTIONS]` for them. `argx_command_validate()` reports an option that reuses a name of a global option of an ancestor as `ARGX_ERROR_NAME_CONFLICT`. Schemas without global options parse and print as before. ([#9](https://github.com/vabelorg/argx/issues/9))
- List options, `ARGX_OPTION_LIST`, added with `argx_command_add_list()`: an option that keeps every value it is given, in command-line order, for options such as `-I dir -I dir2` or `--exclude a --exclude b`. `argx_option_set_separator()` also splits each value into items, so `--keys title,tags` gives two items; an empty item is the new parse error `ARGX_PARSE_ERROR_EMPTY_ITEM`. `argx_option_add_default()` adds default items, which are used only when the option is not given. Read the values with `argx_result_option_value_count()`, `argx_result_option_string_at()`, `_int_at()`, `_float_at()`, `_choice_at()` and `_value_at()`, which also read an option with a value. `argx_result_option_arg_index()` and `argx_result_positional_arg_index()` give the index in argv of each value, so a program can merge the order of different options and positionals. `argx_option_separator()`, `argx_option_default_count()` and `argx_option_default_at()` read the new attributes. The help shows a list option as one occurrence, `--keys <KEY>[,<KEY>...]` with a separator, with the annotation `[repeatable]`. ([#3](https://github.com/vabelorg/argx/issues/3))

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
