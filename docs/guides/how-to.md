# How-to

Short recipes for specific tasks. To learn ArgX from the start, read the [Tutorial](../getting-started/tutorial.md). For why ArgX behaves as it does, see the [FAQ](faq.md).

## Command lines

### Allowing a command line without a subcommand

Subcommands are required by default: a command line that ends in a command with subcommands is the parse error `MISSING_SUBCOMMAND`. For a command that does something of its own without one, such as `git remote` listing the remotes, make them optional:

```c
TRY(argx_command_set_subcommand_optional(remote, true));
```

The command line may then end in `remote`, `argx_result_command` returns it, and its usage line shows `[COMMAND]` instead of `<COMMAND>` ([Subcommands](../references/syntax.md#subcommands)). The setting belongs to `remote` only. Handle that case where you branch on `argx_result_command`, as `run` in [examples/pack/pack.c](../../examples/pack/pack.c) does for `create` and `list`.

### Defining an option for the whole program

Options such as `--verbose` or `--config` belong to the whole program, and users write them before or after the subcommand. Define them once, on the root, and make them global:

```c
TRY(argx_command_add_count(root, 'v', "verbose", &verbose));
TRY(argx_option_set_global(verbose, true));
```

Every subcommand at any depth then knows `-v`, and `argx_result_count(result, verbose)` counts all of them: `pack -v create -vv` gives 3. The help of each subcommand lists it under `Global options:`. A subcommand cannot define another `-v` or `--verbose`: validation reports it as `ARGX_ERROR_NAME_CONFLICT` ([Global options](../references/syntax.md#global-options)).

### Printing an error message when no subcommand is given

`examples/pack/pack.c` prints the help when a command line ends without a subcommand, as most programs with subcommands do. To print the message of the parse error and the usage line instead, like any other parse error, drop that branch from the `ARGX_ERROR_INVALID_COMMAND_LINE` case:

```c
case ARGX_ERROR_INVALID_COMMAND_LINE:
    print_parse_error(result);
    break;
```

`pack` alone then prints:

```
pack: missing required command
Usage: pack [OPTIONS] <COMMAND>
For more information, try '--help'.
```

### Passing a value that starts with `-`

Write it after `--`: `pack create -o out.tar -- -notes.txt`. A negative number such as `-1` is a value even without `--`, unless a digit is a short option name known in the command, its own or a global option of an ancestor, ([How elements are classified](../references/syntax.md#how-elements-are-classified)).

### Wide-character argv with `wmain`

To receive non-ASCII arguments on Windows, pass the argv of `wmain` to `argx_parse_wide` ([Wide-character argv](../references/parse.md#wide-character-argv)). Strings read from the result are UTF-8:

```c
int wmain(int argc, wchar_t **argv)
{
    ...
    error = argx_parse_wide(root, argc, argv, &result);
    ...
    if (argx_result_positional_string(result, name, 0, &text))
    {
        printf("hello, %s\n", text);
    }
    ...
}
```

To display them, set the console output code page yourself.

## Values

### Reading the value of a user-defined value type

Read the pointer from `argx_result_option_value` or `argx_result_positional_value` as the C type your conversion function wrote ([Reading the result](../references/parse.md#reading-the-result)). For the `size` type of `examples/pack/pack.c`, which writes an `unsigned long`:

```c
const unsigned long *block_size = argx_result_option_value(result, schema->block_size);

if (block_size != NULL)
{
    printf("block size: %lu\n", *block_size);
}
```

## Help

### Fitting the help to the terminal

ArgX never reads the terminal. Get its width in your program and set it on the help options ([Help options](../references/help.md#help-options)):

```c
argx_help_options_t *options = NULL;

if (argx_help_options_create(&options) == ARGX_ERROR_NONE)
{
    argx_help_options_set_width(options, terminal_width);
    argx_help_write(command, options, write_to_stream, stdout);
    argx_help_options_destroy(options);
}
```

### Measuring the display width of the help

By default one byte is one column, so a help with non-ASCII characters wraps early and its columns are too wide. Give the help options a [measure function](../references/help.md#measure-function) that returns the display width of a text. ArgX keeps no data about characters, so the function decides, for example, how wide an ambiguous-width character is.

With POSIX `wcwidth()`, in a UTF-8 locale (call `setlocale(LC_CTYPE, "")` first). The complete program is [examples/measure/measure.c](../../examples/measure/measure.c):

```c
#define _XOPEN_SOURCE 700 /* for wcwidth() */
#include <string.h>
#include <wchar.h>

static size_t measure_with_wcwidth(void *user_data, const char *text, size_t length)
{
    mbstate_t state;
    size_t total = 0;

    (void)user_data;
    memset(&state, 0, sizeof state);
    while (length != 0)
    {
        wchar_t wc;
        size_t n = mbrtowc(&wc, text, length, &state);
        int w;

        if ((n == (size_t)-1) || (n == (size_t)-2))
        {
            /* Invalid or cut short: one column for the byte. */
            memset(&state, 0, sizeof state);
            n = 1;
            w = 1;
        }
        else
        {
            if (n == 0)
            {
                n = 1;
            }
            w = wcwidth(wc);
        }
        total += (w < 0) ? 1 : (size_t)w;
        text += n;
        length -= n;
    }
    return total;
}
```

```c
argx_help_options_set_measure(options, measure_with_wcwidth, NULL);
```

`wcwidth()` is not available on Windows, and on some systems `wchar_t` is UTF-16. A portable choice is [utf8proc](https://github.com/JuliaStrings/utf8proc), which does not depend on the locale:

```c
#include <utf8proc.h>

static size_t measure_with_utf8proc(void *user_data, const char *text, size_t length)
{
    const utf8proc_uint8_t *bytes = (const utf8proc_uint8_t *)text;
    size_t total = 0;

    (void)user_data;
    while (length != 0)
    {
        utf8proc_int32_t code_point;
        utf8proc_ssize_t n = utf8proc_iterate(bytes, (utf8proc_ssize_t)length, &code_point);

        if (n < 1)
        {
            /* Invalid: one column for the byte. */
            n = 1;
            total += 1;
        }
        else
        {
            total += (size_t)utf8proc_charwidth(code_point);
        }
        bytes += n;
        length -= (size_t)n;
    }
    return total;
}
```

Both count each character on its own, so an emoji sequence joined with ZWJ, or a flag, counts wider than most terminals show it. To count grapheme clusters, use `utf8proc_grapheme_break_stateful()` in the same loop.

### Writing help text in Chinese or Japanese

ArgX breaks lines only at blanks, so a sentence without blanks is one word and is never broken. Break the lines yourself with `\n`, where they read well. A line after `\n` starts at the column where the description started, and a line that fits the width is never broken again, so set a [measure function](#measuring-the-display-width-of-the-help) too:

```c
argx_option_set_description(output, "変換した画像を書き込むファイル。\n"
                                    "省略すると、入力と同じ場所に書き込みます。");
```

```
Options:
  -o, --output <出力>  変換した画像を書き込むファイル。
                       省略すると、入力と同じ場所に書き込みます。
```

Keep each line short enough for the narrowest width you expect: the description column plus the line must fit, or the line runs past the width. This is how git and GNU coreutils handle translated help. Translators of a PO file can break the lines the same way.

### Getting the help as a string

Call [`argx_help_format`](../references/help.md#formatting-into-a-buffer) once for the length and once for the text. Unlike `argx_help_write`, it never leaves partial output.

```c
/* Returns the help as a string to free(), or NULL. */
static char *help_string(const argx_command_t *command)
{
    char *text = NULL;
    size_t length = 0;

    if (argx_help_format(command, NULL, NULL, 0, &length) != ARGX_ERROR_NONE)
    {
        return NULL;
    }

    text = malloc(length + 1);
    if (text == NULL)
    {
        return NULL;
    }

    if (argx_help_format(command, NULL, text, length + 1, NULL) != ARGX_ERROR_NONE)
    {
        free(text);
        return NULL;
    }
    return text;
}
```

### Printing a parse error with the usage line from a fixed buffer

`argx_help_format_usage` cannot fail; it cuts the line when it does not fit.

```c
static void print_error(const argx_command_t *command, const char *message)
{
    char usage[256];
    size_t length = 0;

    argx_help_format_usage(command, NULL, usage, sizeof(usage), &length);

    if (length >= sizeof(usage))
    {
        fprintf(stderr, "pack: %s\n", message);
    }
    else
    {
        fprintf(stderr, "pack: %s\n%s", message, usage);
    }
}
```

### Naming the command in the hint after a parse error

`examples/pack/pack.c` prints `For more information, try '--help'.` To name the command where the error was found, walk up from [`argx_result_error_command`](../references/parse.md#parse-error-details) with [`argx_command_parent`](../references/schema.md#reading-a-schema), which returns `NULL` for the root:

```c
/* Writes the names of the commands from the root to command, separated by spaces. */
static void print_command_path(FILE *stream, const argx_command_t *command)
{
    const argx_command_t *parent = argx_command_parent(command);

    if (parent != NULL)
    {
        print_command_path(stream, parent);
        fputc(' ', stream);
    }
    fputs(argx_command_name(command), stream);
}

/* After the message and the usage line: */
fputs("For more information, try '", stderr);
print_command_path(stderr, argx_result_error_command(result));
fputs(" --help'.\n", stderr);
```

For `pack create --colour`, this prints `For more information, try 'pack create --help'.` The command named must know `--help`: it defines it, or an ancestor defines it as a global option, as `examples/pack/pack.c` does ([Why is `--help` an unknown option after a subcommand?](faq.md#why-is---help-an-unknown-option-after-a-subcommand)).
