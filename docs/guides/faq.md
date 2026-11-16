# FAQ

Why ArgX behaves as it does. For step-by-step tasks, see [How-to](how-to.md).

## Command lines

### Why is `--help` an unknown option after a subcommand?

An option is recognized only on the command that defines it, unless it is global, and the library adds no `--help` of its own. Make the `--help` of the root global with `argx_option_set_global`: every subcommand then knows it ([`--help`](../references/parse.md#--help), [Global options](../references/syntax.md#global-options)).

### Why does `pack create --help` report a missing required option?

Because that `--help` is not terminal. A flag like any other is read, and then the parse checks what is required. Make `--help` terminal with `argx_option_set_terminal`: the parse then ends at `--help` and succeeds, and nothing required is checked ([Terminal flags](../references/parse.md#terminal-flags), [Tutorial](../getting-started/tutorial.md#2-define-the-schema)).

### Why is a command line without a subcommand a parse error?

Subcommands are required by default, as most programs with subcommands expect, and the usage line shows `<COMMAND>`. What is printed is up to the program: `examples/pack/pack.c` prints the help. Make the subcommands optional per command with `argx_command_set_subcommand_optional` ([How-to](how-to.md#allowing-a-command-line-without-a-subcommand)).

### Where do I write an option: before or after the subcommand?

After the name of the command that defines it. A [global option](../references/syntax.md#global-options) may also go after the name of any subcommand below that command; other options are not known there ([Subcommands](../references/syntax.md#subcommands)).

### Why does validation report a name conflict between two commands?

A subcommand has an option with a name of a global option of one of its ancestors. A global option is known on every command below its own, so the same name there would be ambiguous. Rename one of the two options, or make the ancestor's option not global ([Global options](../references/syntax.md#global-options)).

### What happens when an option is given more than once?

A flag stays given, a count counts every time, and the others take the last one ([Repeated options](../references/syntax.md#repeated-options)).

### Can values come from environment variables?

No. Values come only from the command line and the defaults.

## Values

### Why does validation fail with `ARGX_ERROR_INVALID_DEFAULT`?

A value type cannot convert the default value of its option or positional. The output parameters of `argx_command_validate` tell which one ([Validation](../references/schema.md#validation)).

## Help

### Can I translate the help, add colors, or hide an option?

No. The fixed phrases are English only, and the parts cannot be colored, reordered, hidden or grouped ([Limits](../references/help-layout.md#limits)).

### Why does a description with non-ASCII characters wrap too early?

Without a measure function, widths are counted in bytes, not display columns. Set a measure function on the help options ([How-to](how-to.md#measuring-the-display-width-of-the-help)).

### Why is a description in Chinese or Japanese never broken?

Lines break only at blanks, and such text has none. Break its lines with `\n` ([How-to](how-to.md#writing-help-text-in-chinese-or-japanese)).

## Debugging

### Why did a wrong call not stop my program?

Preconditions are checked with `assert` only in Debug; in Release, breaking one is undefined behavior ([Runtime failures and programming errors](../references/ownership.md#runtime-failures-and-programming-errors)).
