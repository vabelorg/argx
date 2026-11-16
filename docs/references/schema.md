# Schemas

API reference of `include/argx/schema.h`. A schema is a tree of commands, each with options and positionals, that describes the command lines your program accepts.

## Functions

| Functions | Section |
| --- | --- |
| `argx_command_create`, `argx_command_destroy`, `argx_command_add_subcommand`, `argx_command_set_description`, `argx_command_set_summary`, `argx_command_set_subcommand_optional` | [Commands](#commands) |
| `argx_command_add_flag`, `argx_command_add_negatable`, `argx_command_add_count`, `argx_command_add_value`, `argx_command_add_list` | [Options](#options) |
| `argx_option_set_description`, `argx_option_set_value_name`, `argx_option_set_required`, `argx_option_set_default`, `argx_option_add_default`, `argx_option_set_separator`, `argx_option_set_initial`, `argx_option_set_terminal`, `argx_option_set_global` | [Attributes](#attributes) |
| `argx_command_add_positional`, `argx_positional_set_description`, `argx_positional_set_default` | [Positionals](#positionals) |
| `argx_command_validate`, `argx_command_is_validated` | [Validation](#validation) |
| `argx_command_name`, `argx_command_description`, `argx_command_summary`, `argx_command_is_subcommand_optional`, `argx_command_parent`, `argx_command_option_count`, `argx_command_option_at`, `argx_command_positional_count`, `argx_command_positional_at`, `argx_command_subcommand_count`, `argx_command_subcommand_at`, `argx_command_find_short`, `argx_command_find_long`, `argx_command_find_subcommand` | [Reading a schema](#reading-a-schema) |
| `argx_option_command`, `argx_option_kind`, `argx_option_short_name`, `argx_option_long_name`, `argx_option_negated_name`, `argx_option_type`, `argx_option_description`, `argx_option_value_name`, `argx_option_is_required`, `argx_option_default`, `argx_option_default_count`, `argx_option_default_at`, `argx_option_separator`, `argx_option_initial`, `argx_option_is_terminal`, `argx_option_is_global` | [Reading a schema](#reading-a-schema) |
| `argx_positional_command`, `argx_positional_name`, `argx_positional_arity`, `argx_positional_type`, `argx_positional_description`, `argx_positional_default` | [Reading a schema](#reading-a-schema) |

## Commands

| To do this | Function | Notes |
| --- | --- | --- |
| Create the root | `argx_command_create` | Any non-empty name |
| Add a subcommand | `argx_command_add_subcommand` | An ASCII letter or digit, then letters, digits, `-` or `_` |
| Set the description | `argx_command_set_description` | May be empty |
| Set the summary for the parent's `Commands:` list | `argx_command_set_summary` | May be empty |
| Let a command line end without a subcommand | `argx_command_set_subcommand_optional` | `false` until set |
| Destroy the tree | `argx_command_destroy` | On the root only. `NULL` does nothing |

- Nesting has no depth limit.
- A command cannot have both positionals and subcommands.
- **Subcommands are required by default.** A command line that ends in a command with subcommands, without naming one, is the parse error `MISSING_SUBCOMMAND`, and the usage line shows `<COMMAND>`. Set `argx_command_set_subcommand_optional(command, true)` for a command that does something of its own without a subcommand, such as `git remote`; the command line then ends in that command, and the usage line shows `[COMMAND]` ([Subcommands](syntax.md#subcommands)).
- Making the subcommands of a command without subcommands optional is a validation error, since they can be added after the call.
- The parent's `Commands:` list shows the summary if it is set, and otherwise only the first line of the description. The command's own help shows the whole description and never the summary. A root's summary is not shown, since the root is in no list ([Help Text Layout](help-layout.md#structure)).

## Options

| Kind | Add function | Meaning |
| --- | --- | --- |
| `ARGX_OPTION_FLAG` | `argx_command_add_flag` | Whether it was given |
| `ARGX_OPTION_NEGATABLE` | `argx_command_add_negatable` | Switched with `--name` and `--no-name` |
| `ARGX_OPTION_COUNT` | `argx_command_add_count` | How many times it was given |
| `ARGX_OPTION_VALUE` | `argx_command_add_value` | Takes one value of a value type; the last one given is kept |
| `ARGX_OPTION_LIST` | `argx_command_add_list` | Takes one value of a value type each time it is given; every one is kept, in command-line order |

- An option with a value and a list option **take a value**: both have a value type, a value name and can be required.
- A list option is read by index ([List options](parse.md#list-options)); the readers without an index require an option that is not a list.
- Use a list option for an option given once per item, such as `-I dir`, `-D NAME=1` or `--exclude pattern`, or for one value that holds several items, such as `--keys title,tags`, with a separator.

### Names

- An option has a short name (`'\0'` if none), a long name (`NULL` if none), or both.
- A short name is one ASCII letter or digit. A long name is two or more characters: an ASCII letter or digit, then letters, digits, `-` or `_`.
- A negatable flag must have a long name; its negated name `no-<long name>` takes part in conflict checks.

### Attributes

| Attribute | Set function | Applies to | When not set |
| --- | --- | --- | --- |
| Description | `argx_option_set_description` | All | `NULL` |
| Value name | `argx_option_set_value_name` | `ARGX_OPTION_VALUE`, `ARGX_OPTION_LIST` | `NULL` |
| Required | `argx_option_set_required` | `ARGX_OPTION_VALUE`, `ARGX_OPTION_LIST` | `false` |
| Default value | `argx_option_set_default` | `ARGX_OPTION_VALUE` | `NULL` |
| Default items (added, not set) | `argx_option_add_default` | `ARGX_OPTION_LIST` | None |
| Separator | `argx_option_set_separator` | `ARGX_OPTION_LIST` | `'\0'` (no split) |
| Initial state | `argx_option_set_initial` | `ARGX_OPTION_NEGATABLE` | `false` |
| Terminal | `argx_option_set_terminal` | `ARGX_OPTION_FLAG` | `false` |
| Global | `argx_option_set_global` | All | `false` |

- Setting an attribute on a kind it does not apply to is an error, whatever the value.
- A default value is written as on the command line; validation checks that it converts.
- Required together with a default, or with default items, is a validation error. A required list option must be given at least once.
- `argx_option_add_default` appends one item each call; there is no way to remove one. A list option that is given on the command line gets none of them ([Required checks and default values](syntax.md#required-checks-and-default-values)). A default item is one item: it is not split at the separator.
- The separator is `'\0'` or one of the 32 ASCII punctuation characters (`!"#$%&'()*+,-./:;<=>?@[\]^_\`{|}~`), whatever the locale; any other character is `ARGX_ERROR_INVALID_DEFINITION`. How values are split is in [Splitting list values](syntax.md#splitting-list-values).
- A terminal flag ends the parse when it is read: the parse succeeds without reading the arguments after it and without checking what is required. Use it for `--help` and `--version` ([Terminal flags](parse.md#terminal-flags)). It does not change the help.
- A global option is known on its command and on every descendant of that command, so it can be given after the name of any of them. Use it for options of the whole program, such as `--help`, `--verbose` or `--config` ([Global options](syntax.md#global-options)). It is still one option of its command: one handle reads it wherever it was given, and its required check and default value are its command's. The help of a descendant lists it under `Global options:` ([Structure](help-layout.md#structure)).
- The description and value name only affect the help. Without a value name, the type name is shown. A newline in a value name breaks the help columns ([Limits](help-layout.md#limits)).

## Positionals

`argx_command_add_positional` takes a name, a value type and an arity. Positionals take arguments in the order they were added.

| Arity | Values |
| --- | --- |
| `ARGX_ARITY_REQUIRED` | Exactly one |
| `ARGX_ARITY_OPTIONAL` | Zero or one |
| `ARGX_ARITY_ZERO_OR_MORE` | Zero or more |
| `ARGX_ARITY_ONE_OR_MORE` | One or more |

- The name is any non-empty string, unique within the command. It appears only in the help ([Usage line](help-layout.md#usage-line)); a newline in it breaks the columns.
- The description can be set for any arity; a default value only for `ARGX_ARITY_OPTIONAL`.
- The order of arities is checked by validation, not when adding.

## Rules common to all definitions

- **Strings are copied.**
- **Setting again replaces.** No function clears a setting.
- **Empty is not unset.** `""` differs from `NULL`.
- **Names are compared byte by byte,** case-sensitively.
- **Conflicts are checked per command,** with one exception. A parent and a child, or two subcommands, may reuse names, but no option below a command may reuse a name of that command's global options; validation checks it, since an option can be made global after its descendants are added.
- **The root owns the whole tree.** Handles are never destroyed on their own.
- **Nothing can be removed or copied.**

## Validation

```
argx_error_t argx_command_validate(argx_command_t *root,
                                   const argx_command_t **command,
                                   const argx_option_t **option,
                                   const argx_positional_t **positional);
bool argx_command_is_validated(const argx_command_t *command);
```

`argx_command_validate` checks the whole tree and returns the first error in a fixed [order](#order-of-the-checks).

### The output parameters

| Return value | `*command` | `*option` | `*positional` | Validation state |
| --- | --- | --- | --- | --- |
| `ARGX_ERROR_NONE` | `NULL` | `NULL` | `NULL` | Validated |
| `ARGX_ERROR_NAME_CONFLICT`, `ARGX_ERROR_INVALID_DEFINITION`, `ARGX_ERROR_INVALID_DEFAULT` (option) | Its command | The option | `NULL` | Unchanged |
| `ARGX_ERROR_INVALID_DEFINITION`, `ARGX_ERROR_INVALID_DEFAULT` (positional) | Its command | `NULL` | The positional | Unchanged |
| `ARGX_ERROR_INVALID_DEFINITION` (command) | The command | `NULL` | `NULL` | Unchanged |
| `ARGX_ERROR_OUT_OF_MEMORY` | `NULL` | `NULL` | `NULL` | Unchanged |

- Each output may be `NULL`; a non-`NULL` one is always written. The handles live until the root is destroyed.
- Validation never changes the definition, so it can simply be repeated after a fix or after `ARGX_ERROR_OUT_OF_MEMORY`.

### The validation state

- A new root is not validated. A successful validation validates the tree; a failed one leaves the state unchanged.
- Any successful add or set anywhere in the tree, even of the same value, makes it not validated. A failed one does not.
- `argx_command_is_validated` gives the same answer for any command of the tree.
- Parsing requires a validated schema (a precondition, [Parsing](parse.md#preconditions)).

### What validation calls

For each default value, validation converts it into a temporary buffer and releases it on success. Conversion and release functions are therefore called on every validation, and no value is kept.

## Definition errors

Errors the add and set functions can see are returned at once; errors that need the whole tree are returned by validation. Codes and messages are in [Error Codes](errors.md).

A failed add or set leaves the schema and its validation state unchanged, writes a `NULL` handle, and leaks nothing. The schema stays usable, and the same name can be added again.

### Errors when adding and setting

Within a function, the first matching row is returned. Every function can also return `ARGX_ERROR_OUT_OF_MEMORY`, which is the only failure of setting a description or a summary.

**`argx_command_create`**

| Condition | Error code |
| --- | --- |
| The name is empty | `ARGX_ERROR_INVALID_NAME` |

**`argx_command_add_subcommand`**

| Condition | Error code |
| --- | --- |
| The name breaks the rules or is empty | `ARGX_ERROR_INVALID_NAME` |
| The command has positionals | `ARGX_ERROR_INVALID_DEFINITION` |
| The command has a subcommand with the same name | `ARGX_ERROR_NAME_CONFLICT` |

**`argx_command_add_flag`, `argx_command_add_negatable`, `argx_command_add_count`, `argx_command_add_value`, `argx_command_add_list`**

| Condition | Error code |
| --- | --- |
| Neither a short nor a long name | `ARGX_ERROR_INVALID_NAME` |
| A negatable flag without a long name | `ARGX_ERROR_INVALID_NAME` |
| A name breaks the rules | `ARGX_ERROR_INVALID_NAME` |
| The short name is already used in the command | `ARGX_ERROR_NAME_CONFLICT` |
| The long name equals a long or negated name in the command | `ARGX_ERROR_NAME_CONFLICT` |
| The negated name equals a long or negated name in the command | `ARGX_ERROR_NAME_CONFLICT` |

**`argx_command_add_positional`**

| Condition | Error code |
| --- | --- |
| The name is empty | `ARGX_ERROR_INVALID_NAME` |
| The command has subcommands | `ARGX_ERROR_INVALID_DEFINITION` |
| The command has a positional with the same name | `ARGX_ERROR_NAME_CONFLICT` |

**Setting attributes**

| Function | Condition | Error code |
| --- | --- | --- |
| `argx_option_set_value_name`, `argx_option_set_required` | Neither an option with a value nor a list option | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_option_set_default` | Not an option with a value (a list option included) | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_option_add_default` | Not a list option | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_option_set_separator` | Not a list option | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_option_set_separator` | Neither `'\0'` nor ASCII punctuation | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_option_set_initial` | Not a negatable flag | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_option_set_terminal` | Not a flag | `ARGX_ERROR_INVALID_DEFINITION` |
| `argx_positional_set_default` | Arity is not `ARGX_ARITY_OPTIONAL` | `ARGX_ERROR_INVALID_DEFINITION` |

### Errors found by validation

| Condition | Error code | Reported element |
| --- | --- | --- |
| `ARGX_ARITY_REQUIRED` or `ARGX_ARITY_ONE_OR_MORE` after `ARGX_ARITY_OPTIONAL` or `ARGX_ARITY_ZERO_OR_MORE` | `ARGX_ERROR_INVALID_DEFINITION` | The later positional |
| Any positional after `ARGX_ARITY_ZERO_OR_MORE` or `ARGX_ARITY_ONE_OR_MORE` | `ARGX_ERROR_INVALID_DEFINITION` | The later positional |
| A name of an option is a name of a global option of an ancestor | `ARGX_ERROR_NAME_CONFLICT` | The option of the descendant |
| An option is required and has a default or default items | `ARGX_ERROR_INVALID_DEFINITION` | The option |
| A list option with a separator has a default item that is empty or contains the separator | `ARGX_ERROR_INVALID_DEFINITION` | The option |
| An option's default, or one of its default items, does not convert | `ARGX_ERROR_INVALID_DEFAULT` | The option |
| A positional's default does not convert | `ARGX_ERROR_INVALID_DEFAULT` | The positional |
| A command's subcommands are optional, and it has none | `ARGX_ERROR_INVALID_DEFINITION` | The command |
| An allocation failed, or a conversion returned `ARGX_ERROR_OUT_OF_MEMORY` | `ARGX_ERROR_OUT_OF_MEMORY` | None |

Any other conversion failure becomes `ARGX_ERROR_INVALID_DEFAULT`.

### Order of the checks

Commands are checked depth first from the root, subcommands in the order added. Within a command:

1. The order of positional arities.
2. Each option in the order added: a name of a global option of an ancestor, then required with a default, then a default item that the separator would split, then the conversion of the default or of each default item, in the order added.
3. Each positional's default conversion, in the order added.
4. Optional subcommands without a subcommand.

## Guarantees of a validated schema

Every schema satisfies these, since the add and set functions enforce them:

- Within a command, short names, long and negated names together, subcommand names and positional names are each unique.
- Every option has a short or long name; a negatable flag has a long and a negated name.
- Names follow the rules, and the root and positional names are not empty.
- No command has both positionals and subcommands.
- Exactly the options with a value, the list options and the positionals have a value type.
- Attributes are set only where they apply.

A validated schema also satisfies, for every command:

- The positionals are either zero or more `REQUIRED`, zero or more `OPTIONAL`, and at most one `ZERO_OR_MORE`, or zero or more `REQUIRED` and one `ONE_OR_MORE`.
- No option uses a name of a global option of an ancestor.
- No required option has a default or default items.
- No default item of a list option with a separator is empty or contains the separator.
- Only a command with subcommands has optional subcommands.
- Every default converted successfully at validation, so it will again if the conversion is deterministic.

## Reading a schema

The read functions take `const` handles, change nothing and never fail.

| Object | Functions |
| --- | --- |
| Command | `argx_command_name`, `argx_command_description`, `argx_command_summary`, `argx_command_is_subcommand_optional`, `argx_command_parent`, `argx_command_is_validated` |
| Enumeration | `argx_command_option_count`/`_at`, `argx_command_positional_count`/`_at`, `argx_command_subcommand_count`/`_at` |
| Lookup | `argx_command_find_short`, `argx_command_find_long`, `argx_command_find_subcommand` |
| Option | `argx_option_command`, `argx_option_kind`, `argx_option_short_name`, `argx_option_long_name`, `argx_option_negated_name`, `argx_option_type`, `argx_option_description`, `argx_option_value_name`, `argx_option_is_required`, `argx_option_default`, `argx_option_default_count`/`_at`, `argx_option_separator`, `argx_option_initial`, `argx_option_is_terminal`, `argx_option_is_global` |
| Positional | `argx_positional_command`, `argx_positional_name`, `argx_positional_arity`, `argx_positional_type`, `argx_positional_description`, `argx_positional_default` |

- Enumeration follows the order added, from index 0.
- Unset strings and handles read as `NULL`, a missing short name as `'\0'`, and an attribute that does not apply to the kind as `NULL`, `false`, `'\0'` or 0.
- `argx_option_default_count` and `argx_option_default_at` read the default items of a list option, and the default of an option with a value as one item (count 0 or 1), so the help and other readers can treat both alike.
- Lookups search only the given command, comparing whole names, not the global options of its ancestors. `argx_command_find_long` matches the first `length` bytes of `name` (so `name=value` needs no copy), also matches negated names, and writes which matched to `*negated` when found. `argx_command_find_short('\0')` returns `NULL`.
- Returned strings and handles live until the root is destroyed, except that a description, summary, value name or default lives only until that attribute is set again. Default items live until the root is destroyed.
- Reads may run concurrently while the schema is not changed. Changing or validating must not overlap any other call on the schema.

## Preconditions

The full list is under `Preconditions:` in the header ([programming errors](ownership.md#runtime-failures-and-programming-errors)).

- Pointer arguments are not `NULL`, except the argument of a destroy function, the `out` of an add function, `long_name` (no long name), `negated` of `argx_command_find_long`, and the outputs of `argx_command_validate`.
- An `index` is less than the count, and `arity` is a defined constant.
- `argx_command_destroy` and `argx_command_validate` take the root.
- Handles are not used after destruction, and value types outlive their schemas ([Value Types](types.md#borrowed-value-types-and-the-order-of-destruction)).
