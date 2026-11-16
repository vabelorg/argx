# Help Text Layout

The text that the functions of [Help and Usage](help.md) generate. It depends only on the schema and the help options, never on the locale, the environment or the terminal.

The rules assume names, value names, defaults and choices without leading or trailing blanks or newlines, and descriptions that do not start with a newline or a blank line; see [Descriptions](#descriptions) and [Limits](#limits) for the rest.

The examples use `pack create`, with the description `Create an archive.` and these definitions in order:

| Definition | Kind | Value name | Attributes | Description |
| --- | --- | --- | --- | --- |
| `-h, --help` | Flag | | | `Print this help.` |
| `-o, --output` | Option with a value, string | `FILE` | Required | `The archive to write.` |
| `-l, --level` | Option with a value, integer | `LEVEL` | Default value `6` | `The compression level.` |
| `file` | Positional, one or more, string | | | `The files to pack.` |

```
Usage: pack create [OPTIONS] --output <FILE> <file>...   <- usage line

Create an archive.                                       <- description of the command

Arguments:                                               <- list
  <file>...            The files to pack.                <- item

Options:
  -h, --help           Print this help.
  -o, --output <FILE>  The archive to write. [required]
  ^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^
  heading              description text      annotation
  |                    |
  indent (column 2)    description column (column 23)
```

- A **list** is `Commands:`, `Arguments:`, `Options:` or `Global options:`; an **item** is one entry in it. An item's **description** is its description text plus its annotations.
- A **column** is an offset from the start of the line, from 0. Widths are measured by the [measure function](help.md#measure-function) of the help options; without one, one byte is one column. Blanks always count one column each.
- Lines end with `\n` only, including the last one, and never with a blank unless a name does.
- Fixed phrases are English. Schema strings are written unchanged; descriptions and annotations are wrapped.

## Structure

| Order | Part | Appears when | Content |
| --- | --- | --- | --- |
| 1 | Usage line | Always | See [Usage line](#usage-line) |
| 2 | Description of the command | It is set | From column 0, wrapped, without a trailing newline |
| 3 | `Commands:` | There are subcommands | The direct subcommands, each with its summary or, without one, the first line of its description |
| 4 | `Arguments:` | There are positionals | The positionals, with full descriptions |
| 5 | `Options:` | There are options | The options, with full descriptions |
| 6 | `Global options:` | An ancestor has global options | The [global options](syntax.md#global-options) of the ancestors, with full descriptions |

- Parts are separated by one empty line, with none at the start or end. Absent parts leave no trace.
- Lists show items in definition order, and only those of the command itself, never the parent's, with one exception: `Global options:` lists the global options of the command's ancestors, the root's first, then those of each command further down, each command's in definition order.
- A command lists its own global options in `Options:`, like any of its options. The global options of its siblings and descendants are not listed.

`pack create`, when the root `pack` defines a global `-v, --verbose` count and a global `--[no-]color`:

```
Usage: pack create [OPTIONS] --output <FILE> <file>...

Create an archive.

Arguments:
  <file>...            The files to pack.

Options:
  -h, --help           Print this help.
  -o, --output <FILE>  The archive to write. [required]

Global options:
  -v, --verbose...     Print more.
      --[no-]color     Color the output. [default: on]
```

A command with nothing defined:

```
Usage: tool
```

## Usage line

| Order | Item | Appears when | Notation |
| --- | --- | --- | --- |
| 1 | Command path | Always | `Usage: ` and the names from the root, separated by blanks |
| 2 | `[OPTIONS]` | There is an option that is not required, of the command or among the global options of its ancestors | `[OPTIONS]` |
| 3 | Required options | Each required option with a value or list option of the command, in order | `--long <value name>`, or `-s <value name>`; for a list option, the value as in its [heading](#headings) |
| 4 | Positionals | Each positional, in order | By arity, below |
| 5 | Subcommand | There are subcommands | `<COMMAND>`, or `[COMMAND]` when they are optional (`argx_command_set_subcommand_optional`) |

| Arity | Notation (also the heading in `Arguments:`) |
| --- | --- |
| `ARGX_ARITY_REQUIRED` | `<name>` |
| `ARGX_ARITY_OPTIONAL` | `[name]` |
| `ARGX_ARITY_ZERO_OR_MORE` | `[name]...` |
| `ARGX_ARITY_ONE_OR_MORE` | `<name>...` |

```
Usage: tool --output <FILE> -j <integer> <source> [target] [extra]...
Usage: tool remote add [OPTIONS] <name>
Usage: tool --keys <KEY>[,<KEY>...] -I <DIR> <file>
```

- A required list option is written once, as one occurrence, with no `...` after it.

- A required global option is in the usage line of its own command only, like every option of an ancestor; a descendant lists it in `Global options:` with `[required]`.
- Items are separated by one blank. When an item would pass the width (if not 0), the blank becomes a newline and 7 blanks, aligning with the command path.
- Items are never split; one wider than the width runs past it on its own line. The command path is never wrapped.

At the width 40:

```
Usage: pack create [OPTIONS]
       --output <FILE> <file>...
```

## Headings

A subcommand's heading is its name; a positional's is its usage notation. An option's heading is:

| Names | Notation |
| --- | --- |
| Short and long | `-s, --long` |
| Short only | `-s` |
| Long only | 4 blanks, then `--long` |

followed by:

| Kind | Notation | Example |
| --- | --- | --- |
| Flag | Nothing | `-h, --help` |
| Negatable flag | `--[no-]long` | `--[no-]color` |
| Count | `...` | `-v, --verbose...` |
| Option with a value | A blank and `<value name>`, or the type name without one | `-o, --output <FILE>`, `-j <integer>` |
| List option without a separator | As an option with a value | `-I, --include <DIR>` |
| List option with a separator | A blank, `<value name>`, then `[`, the separator, `<value name>` and `...]` | `-k, --keys <KEY>[,<KEY>...]`, `-n <integer>[:<integer>...]` |

- A list option's heading shows one occurrence, never `...` after the value. The `[repeatable]` annotation shows that it may be given again ([Annotations](#annotations)).

```
Options:
  -h, --help           Both names
  -q                   A short name only
      --dry-run        A long name only
  -c, --[no-]check     A negatable flag
  -d...                A count with a short name only
  -j <integer>         No value name: the name of the type
      --mode <choice>  A choice [choices: fast, small]
```

## Annotations

Annotations follow the description text of options and positionals, in this order:

| Order | Annotation | Appears when |
| --- | --- | --- |
| 1 | `[choices: a, b, c]` | The value type is a choice; choices in definition order |
| 2 | `[default: value]` | A default value is set |
| 2 | `[default: on]` | A negatable flag is initially on |
| 2 | `[default: a, b]` | A list option has default items; the items in the order added, separated by `, ` |
| 3 | `[repeatable]` | The option is a list option |
| 4 | `[required]` | An option with a value or a list option is required (positionals show it in their heading) |

- Annotations are separated from the text and each other by one blank, and wrap like descriptions.
- Without description text, they start at the description column.
- If the text ends with a newline, they start on the next line at the description column; each extra newline adds an empty line. Without annotations, a trailing newline is not written.

```
Options:
  -m, --mode <MODE>    What to prefer. [choices: fast, small] [default: fast]
  -k, --kind <choice>  [choices: fast, small] [required]
      --[no-]color     Color the output. [default: on]
  -t, --title <TEXT>   The title of the archive.
                       [default: untitled]
```

```
Options:
  -k, --keys <KEY>[,<KEY>...]  Keys to extract. [repeatable] [required]
  -I, --include <DIR>          Add a search directory. [default: /usr/include]
                               [repeatable]
```

- Default items are written unchanged, so an item that holds `, ` cannot be told apart from two items, as for `[choices: ...]`.

## Description column

An item is the indent, the heading, and the description from the description column, which is shared by all lists of one help.

1. **The maximum** is the smaller of the maximum description column option and half the width (rounded down); with width 0, just the option.
2. The maximum is raised to "indent + 2" if lower.
3. **The description column** is "indent + widest heading + 2", capped at the maximum.

If "indent + heading + 2" fits within the column, the description follows on the same line; otherwise it starts on the next line at the column.

```
Options:
  -v, --verbose...              Say more
      --a-rather-long-name-of-option <FILE>
                                Write to FILE
      --exactly-two-blanks-fit  On the line of the heading
      --one-blank-is-not-enough
                                On the next line
```

## Wrapping

Descriptions stay within the width, with continuation lines at the starting column (0 for the command, the description column for items).

- A word is a run of bytes other than blank (0x20) and newline (0x0A); tabs are part of words.
- Lines break only at blanks, before the word that would pass the width. Words are never split; a first word that is too wide runs past the width.
- A newline always starts a new line; consecutive newlines give empty lines.
- Blanks inside a line and after a newline are kept (use them to indent); blanks at a break or a line end are not written.
- With the width 0, lines break only at newlines.

```
Options:
  -x, --exclude <PATTERN>  Leave out the files that match:
                             *.o    one kind of file
                             build/ a directory
  -u, --url                See
                           https://example.com/a/very/long/address/that/has/no/blank/in/it/at/all
                           for more.
```

## Descriptions

- **A description or summary of only blanks and newlines counts as not set.** In `Commands:`, a subcommand without a summary whose first line is blank shows only its heading.
- **A summary is shown whole in `Commands:`,** wrapped like a description, with its trailing blanks and newlines dropped. It never appears in the command's own help.
- **Trailing blanks are dropped; trailing newlines are kept:** `"foo\n  "` is `"foo\n"`.
- **A leading newline or blank line is kept,** and may add empty lines between parts. Its exact output may change in later versions.

## Limits

- **Without a measure function, widths are counted in bytes,** so non-ASCII text wraps early and widens the description column; set one that counts display columns ([How-to](../guides/how-to.md#measuring-the-display-width-of-the-help)). Tabs and control characters count as the measure function says, one column per byte by default.
- **Lines break only at blanks and newlines.** Text in a script written without blanks, such as Chinese or Japanese, is one word and is never broken; break its lines yourself with `\n` ([How-to](../guides/how-to.md#writing-help-text-in-chinese-or-japanese)).
- **A newline in a name breaks the columns.** Names and value names are written as they are, newlines included, but still measured as one line. Do not put newlines in names.
- **Blanks in names and `\r` anywhere are written unchanged,** so a root named `"tool "` gives `Usage: tool `.
- The fixed phrases cannot be translated; the help cannot be colored, reordered, hidden or grouped, and a subcommand's help never shows the options of its ancestors other than their global options.
