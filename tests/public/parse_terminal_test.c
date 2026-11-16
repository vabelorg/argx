/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * Tests of the public parsing API for terminal flags: argx_parse() of
 * <argx/parse.h> with flags made terminal by argx_option_set_terminal(),
 * read back with the argx_result_*() functions, and
 * argx_result_terminal_option().
 *
 * A terminal flag ends the parse, which succeeds: the arguments after it
 * are not read, and nothing is resolved. The tests cover where the flag
 * may be (alone, after a subcommand, in a bundle), what is not read after
 * it, what is kept from before it, what is not resolved, what still is a
 * parse error, and a command line on which no terminal flag is given.
 *
 * Every test uses the test allocator ("../support/test_allocator.h"). Its
 * state and the allocator that is set are shared by all the tests of this
 * executable. A test starts with an install, destroys what it created, sets
 * the default allocator back before it ends, and ends with check_clean().
 */
#include <argx/argx.h>

#include "../support/test_allocator.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The number of arguments of an argv array. */
#define ARGC(argv) ((int)(sizeof(argv) / sizeof((argv)[0])))

/*
 * The schema of the tests:
 *
 *   pack [-h|--help] [-V|--version] [-q|--quiet] [-v|--verbose]...
 *        [--[no-]color] [--level STRING, "3"] [--name STRING]
 *     create [-h|--help] -o|--output STRING [--mode STRING, "fast"]
 *     extract [-h|--help] ARCHIVE
 *
 * Every --help and --version is terminal; --quiet is a flag that is not.
 * --color is on at first. The subcommands of pack are required, --output
 * is required, and ARCHIVE takes exactly one argument.
 */
typedef struct fixture
{
    argx_command_t *root;
    argx_option_t *help;
    argx_option_t *version;
    argx_option_t *quiet;
    argx_option_t *verbose;
    argx_option_t *color;
    argx_option_t *level;
    argx_option_t *name;
    argx_command_t *create;
    argx_option_t *create_help;
    argx_option_t *output;
    argx_option_t *mode;
    argx_command_t *extract;
    argx_option_t *extract_help;
    argx_positional_t *archive;
} fixture_t;

/*
 * The end of every test: the test allocator has no live block and saw no
 * violation of the allocator contract.
 */
static void check_clean(void)
{
    NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), 0);
    NAOS_CHECK_UINT_EQ(argx_test_allocator_violations(), 0);
}

/* Adds a terminal flag to command. */
static argx_option_t *add_terminal(argx_command_t *command, char short_name, const char *long_name)
{
    argx_option_t *option = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_flag(command, short_name, long_name, &option) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_terminal(option, true) == ARGX_ERROR_NONE);
    return option;
}

/* Adds an option that takes a string to command. */
static argx_option_t *add_string(argx_command_t *command, char short_name, const char *long_name)
{
    argx_option_t *option = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_value(command, short_name, long_name, argx_type_string(), &option) ==
                      ARGX_ERROR_NONE);
    return option;
}

/* Installs the test allocator, builds the schema and validates it. */
static void set_up(fixture_t *fixture)
{
    argx_command_t *root = NULL;

    argx_test_allocator_install();
    NAOS_REQUIRE_TRUE(argx_command_create("pack", &root) == ARGX_ERROR_NONE);
    fixture->root = root;

    fixture->help = add_terminal(root, 'h', "help");
    fixture->version = add_terminal(root, 'V', "version");
    NAOS_REQUIRE_TRUE(argx_command_add_flag(root, 'q', "quiet", &fixture->quiet) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_count(root, 'v', "verbose", &fixture->verbose) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_negatable(root, '\0', "color", &fixture->color) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_initial(fixture->color, true) == ARGX_ERROR_NONE);
    fixture->level = add_string(root, '\0', "level");
    NAOS_REQUIRE_TRUE(argx_option_set_default(fixture->level, "3") == ARGX_ERROR_NONE);
    fixture->name = add_string(root, '\0', "name");

    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(root, "create", &fixture->create) == ARGX_ERROR_NONE);
    fixture->create_help = add_terminal(fixture->create, 'h', "help");
    fixture->output = add_string(fixture->create, 'o', "output");
    NAOS_REQUIRE_TRUE(argx_option_set_required(fixture->output, true) == ARGX_ERROR_NONE);
    fixture->mode = add_string(fixture->create, '\0', "mode");
    NAOS_REQUIRE_TRUE(argx_option_set_default(fixture->mode, "fast") == ARGX_ERROR_NONE);

    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(root, "extract", &fixture->extract) == ARGX_ERROR_NONE);
    fixture->extract_help = add_terminal(fixture->extract, 'h', "help");
    NAOS_REQUIRE_TRUE(argx_command_add_positional(fixture->extract, "ARCHIVE", argx_type_string(),
                                                  ARGX_ARITY_REQUIRED, &fixture->archive) == ARGX_ERROR_NONE);

    NAOS_REQUIRE_TRUE(argx_command_validate(root, NULL, NULL, NULL) == ARGX_ERROR_NONE);
}

/* Destroys the schema and ends the test: nothing stays allocated. */
static void tear_down(fixture_t *fixture)
{
    argx_command_destroy(fixture->root);
    argx_test_allocator_uninstall();
    check_clean();
}

/*
 * Parses argv, which a terminal flag must end: the parse succeeds, with
 * no parse error, the last selected command is command, and the flag that
 * ended it is terminal, which is given. Returns the result, which the
 * caller destroys.
 */
static argx_result_t *parse_stopped(const fixture_t *fixture, int argc, char *const *argv,
                                    const argx_command_t *command, const argx_option_t *terminal)
{
    argx_result_t *result = NULL;

    NAOS_REQUIRE_TRUE(argx_parse(fixture->root, argc, argv, &result) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(result != NULL);
    NAOS_CHECK_INT_EQ(argx_result_error(result), ARGX_ERROR_NONE);
    NAOS_CHECK_INT_EQ(argx_result_error_kind(result), ARGX_PARSE_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_error_message(result) == NULL);
    NAOS_CHECK_TRUE(argx_result_command(result) == command);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == terminal);
    NAOS_CHECK_TRUE(argx_result_flag(result, terminal));
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, terminal), ARGX_SOURCE_COMMAND_LINE);
    return result;
}

/*
 * Parses argv, which must have a parse error of the kind, and no terminal
 * flag that ended it. Returns the result, which the caller destroys.
 */
static argx_result_t *parse_bad(const fixture_t *fixture, int argc, char *const *argv,
                                argx_parse_error_kind_t kind)
{
    argx_result_t *result = NULL;

    NAOS_REQUIRE_TRUE(argx_parse(fixture->root, argc, argv, &result) == ARGX_ERROR_INVALID_COMMAND_LINE);
    NAOS_REQUIRE_TRUE(result != NULL);
    NAOS_CHECK_INT_EQ(argx_result_error_kind(result), kind);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == NULL);
    return result;
}

/* The value of a string option of result, which must have one. */
static const char *string_of(const argx_result_t *result, const argx_option_t *option)
{
    const char *text = NULL;

    NAOS_REQUIRE_TRUE(argx_result_option_string(result, option, &text));
    NAOS_REQUIRE_TRUE(text != NULL);
    return text;
}

/* Checks that an option that takes a value has none in result. */
static void check_no_value(const argx_result_t *result, const argx_option_t *option)
{
    NAOS_CHECK_TRUE(argx_result_option_value(result, option) == NULL);
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, option), ARGX_SOURCE_NONE);
}

/* ---- Where a terminal flag ends the parse --------------------------------- */

/* "pack --help" succeeds, although pack requires a subcommand. */
NAOS_TEST(a_terminal_flag_alone_ends_the_parse_with_success)
{
    fixture_t fixture;
    char *argv[] = { "pack", "--help" };
    char *short_argv[] = { "pack", "-h" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_stopped(&fixture, ARGC(argv), argv, fixture.root, fixture.help);
    NAOS_CHECK_UINT_EQ(argx_result_command_count(result), 1);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.version));
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(short_argv), short_argv, fixture.root, fixture.help);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* What a subcommand requires is not checked: an option and a positional. */
NAOS_TEST(a_terminal_flag_of_a_subcommand_ends_the_parse_in_that_subcommand)
{
    fixture_t fixture;
    char *create[] = { "pack", "create", "--help" };
    char *extract[] = { "pack", "extract", "-h" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_stopped(&fixture, ARGC(create), create, fixture.create, fixture.create_help);
    NAOS_CHECK_UINT_EQ(argx_result_command_count(result), 2);
    NAOS_CHECK_TRUE(argx_result_command_at(result, 0) == fixture.root);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.help));
    check_no_value(result, fixture.output);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(extract), extract, fixture.extract, fixture.extract_help);
    NAOS_CHECK_UINT_EQ(argx_result_positional_count(result, fixture.archive), 0);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* Of two terminal flags, the first ends the parse; the second is not read. */
NAOS_TEST(the_first_terminal_flag_ends_the_parse)
{
    fixture_t fixture;
    char *argv[] = { "pack", "--version", "--help" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_stopped(&fixture, ARGC(argv), argv, fixture.root, fixture.version);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.help));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- What is not read after it -------------------------------------------- */

NAOS_TEST(the_arguments_after_a_terminal_flag_are_not_read)
{
    fixture_t fixture;
    char *subcommand[] = { "pack", "--help", "create", "-o", "x" };
    char *unknown_option[] = { "pack", "--help", "--bogus" };
    char *unknown_subcommand[] = { "pack", "--help", "bogus" };
    char *missing_value[] = { "pack", "--help", "--name" };
    char *options[] = { "pack", "--help", "-v", "--name=n", "--no-color" };
    argx_result_t *result;

    set_up(&fixture);

    /* The subcommand is not selected, so its options are not read either. */
    result = parse_stopped(&fixture, ARGC(subcommand), subcommand, fixture.root, fixture.help);
    NAOS_CHECK_UINT_EQ(argx_result_command_count(result), 1);
    check_no_value(result, fixture.output);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(unknown_option), unknown_option, fixture.root, fixture.help);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(unknown_subcommand), unknown_subcommand, fixture.root,
                           fixture.help);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(missing_value), missing_value, fixture.root, fixture.help);
    argx_result_destroy(result);

    /* Options that would be read are not recorded. */
    result = parse_stopped(&fixture, ARGC(options), options, fixture.root, fixture.help);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 0);
    check_no_value(result, fixture.name);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.color));
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, fixture.color), ARGX_SOURCE_DEFAULT);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * In a bundle, the letters before the terminal flag are read, and the
 * letters after it are not.
 */
NAOS_TEST(a_terminal_flag_ends_a_bundle)
{
    fixture_t fixture;
    char *before[] = { "pack", "-vvh" };
    char *after[] = { "pack", "-hv" };
    char *unknown_after[] = { "pack", "-hx" };
    char *value_after[] = { "pack", "-qhVo" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_stopped(&fixture, ARGC(before), before, fixture.root, fixture.help);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 2);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(after), after, fixture.root, fixture.help);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 0);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(unknown_after), unknown_after, fixture.root, fixture.help);
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(value_after), value_after, fixture.root, fixture.help);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.quiet));
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.version));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- What is kept from before it ------------------------------------------ */

NAOS_TEST(what_was_read_before_a_terminal_flag_is_kept)
{
    fixture_t fixture;
    char *argv[] = { "pack", "-v", "--name", "n", "--no-color", "--level=9", "create", "-o", "out", "-h" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_stopped(&fixture, ARGC(argv), argv, fixture.create, fixture.create_help);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 1);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.name), "n");
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.color));
    NAOS_CHECK_STR_EQ(string_of(result, fixture.level), "9");
    NAOS_CHECK_STR_EQ(string_of(result, fixture.output), "out");
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, fixture.output), ARGX_SOURCE_COMMAND_LINE);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- What is not resolved ------------------------------------------------- */

/*
 * No default value is given, in any selected command. A flag, a count and
 * a negatable flag that were not given read as not given, 0 and the
 * initial state, as after a parse error.
 */
NAOS_TEST(default_values_are_not_given_after_a_terminal_flag)
{
    fixture_t fixture;
    char *root[] = { "pack", "--help" };
    char *create[] = { "pack", "create", "-o", "out", "--help" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_stopped(&fixture, ARGC(root), root, fixture.root, fixture.help);
    check_no_value(result, fixture.level);
    check_no_value(result, fixture.name);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.quiet));
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, fixture.quiet), ARGX_SOURCE_DEFAULT);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 0);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.color));
    argx_result_destroy(result);

    result = parse_stopped(&fixture, ARGC(create), create, fixture.create, fixture.create_help);
    check_no_value(result, fixture.level);
    check_no_value(result, fixture.mode);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- What still is a parse error ------------------------------------------ */

/* A parse error before the terminal flag is reported, and the flag is not read. */
NAOS_TEST(a_parse_error_before_a_terminal_flag_is_reported)
{
    fixture_t fixture;
    char *unknown_option[] = { "pack", "create", "--bogus", "--help" };
    char *unknown_letter[] = { "pack", "-xh" };
    char *unknown_subcommand[] = { "pack", "bogus", "--help" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(unknown_option), unknown_option, ARGX_PARSE_ERROR_UNKNOWN_OPTION);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.create_help));
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(unknown_letter), unknown_letter, ARGX_PARSE_ERROR_UNKNOWN_OPTION);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.help));
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(unknown_subcommand), unknown_subcommand,
                       ARGX_PARSE_ERROR_UNKNOWN_SUBCOMMAND);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* A value for a terminal flag is an error, as for any flag. */
NAOS_TEST(a_value_for_a_terminal_flag_is_an_error)
{
    fixture_t fixture;
    char *argv[] = { "pack", "--help=x" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(argv), argv, ARGX_PARSE_ERROR_UNEXPECTED_VALUE);
    NAOS_CHECK_TRUE(argx_result_error_option(result) == fixture.help);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.help));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * Where an argument is not an option, a terminal flag's name is not one
 * either: as the value of an option that takes a value, and after the
 * terminator.
 */
NAOS_TEST(a_terminal_flag_is_read_only_where_an_option_is)
{
    fixture_t fixture;
    char *value[] = { "pack", "--name", "--help" };
    char *attached[] = { "pack", "create", "-o--help" };
    char *terminator[] = { "pack", "--", "--help" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(value), value, ARGX_PARSE_ERROR_MISSING_SUBCOMMAND);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.name), "--help");
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.help));
    argx_result_destroy(result);

    /* The rest of a bundle after a letter that takes a value. */
    result = NULL;
    NAOS_REQUIRE_TRUE(argx_parse(fixture.root, ARGC(attached), attached, &result) == ARGX_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == NULL);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.output), "--help");
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.create_help));
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(terminator), terminator, ARGX_PARSE_ERROR_UNEXPECTED_POSITIONAL);
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.help));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * A terminal flag is an option of its command like any other: it is not
 * known after a subcommand's name.
 */
NAOS_TEST(a_terminal_flag_of_the_parent_is_unknown_in_a_subcommand)
{
    fixture_t fixture;
    char *argv[] = { "pack", "create", "--version" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(argv), argv, ARGX_PARSE_ERROR_UNKNOWN_OPTION);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- Without a terminal flag ---------------------------------------------- */

/*
 * A command line on which no terminal flag is given is read to its end
 * and resolved, and has no terminal option.
 */
NAOS_TEST(without_a_terminal_flag_the_command_line_is_read_and_resolved)
{
    fixture_t fixture;
    char *argv[] = { "pack", "-q", "create", "-o", "out" };
    char *missing[] = { "pack", "-q" };
    argx_result_t *result = NULL;

    set_up(&fixture);

    NAOS_REQUIRE_TRUE(argx_parse(fixture.root, ARGC(argv), argv, &result) == ARGX_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == NULL);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.level), "3");
    NAOS_CHECK_STR_EQ(string_of(result, fixture.mode), "fast");
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, fixture.mode), ARGX_SOURCE_DEFAULT);
    argx_result_destroy(result);

    /* A flag that is not terminal does not end the parse. */
    result = parse_bad(&fixture, ARGC(missing), missing, ARGX_PARSE_ERROR_MISSING_SUBCOMMAND);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.quiet));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* A flag that was made terminal and then not terminal is an ordinary flag. */
NAOS_TEST(a_flag_that_is_no_longer_terminal_does_not_end_the_parse)
{
    fixture_t fixture;
    char *argv[] = { "pack", "--help" };
    argx_result_t *result;

    set_up(&fixture);
    NAOS_REQUIRE_TRUE(argx_option_set_terminal(fixture.help, false) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_validate(fixture.root, NULL, NULL, NULL) == ARGX_ERROR_NONE);

    result = parse_bad(&fixture, ARGC(argv), argv, ARGX_PARSE_ERROR_MISSING_SUBCOMMAND);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.help));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- A lack of memory ----------------------------------------------------- */

/*
 * Parses argv once for each allocation of the parse, with that allocation
 * failing, and then once with none failing, which must succeed with the
 * terminal flag.
 */
static void check_every_allocation_failing(const fixture_t *fixture, int argc, char *const *argv,
                                           const argx_option_t *terminal)
{
    size_t nth;

    for (nth = 1; nth < 100; nth++)
    {
        const size_t live = argx_test_allocator_live();
        argx_result_t *result = NULL;
        argx_error_t error;

        argx_test_allocator_fail_at(argx_test_allocator_requests() + nth);
        error = argx_parse(fixture->root, argc, argv, &result);
        argx_test_allocator_fail_at(0);

        if (error != ARGX_ERROR_OUT_OF_MEMORY)
        {
            /* The parse made fewer than nth allocations. */
            NAOS_CHECK_INT_EQ(error, ARGX_ERROR_NONE);
            NAOS_REQUIRE_TRUE(result != NULL);
            NAOS_CHECK_TRUE(argx_result_terminal_option(result) == terminal);
            argx_result_destroy(result);
            NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), live);
            NAOS_CHECK_TRUE(nth > 1);
            return;
        }
        NAOS_CHECK_TRUE(result == NULL);
        NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), live);
    }
    NAOS_FAIL("the parse never succeeded");
}

NAOS_TEST(a_parse_that_a_terminal_flag_ends_cannot_allocate_and_leaves_nothing_allocated)
{
    fixture_t fixture;
    char *root[] = { "pack", "-v", "--name", "n", "--help", "--bogus" };
    char *create[] = { "pack", "--level", "9", "create", "-o", "out", "-h", "extract" };

    set_up(&fixture);

    check_every_allocation_failing(&fixture, ARGC(root), root, fixture.help);
    check_every_allocation_failing(&fixture, ARGC(create), create, fixture.create_help);

    tear_down(&fixture);
}

NAOS_SUITE(where,
    NAOS_ADD_TEST(a_terminal_flag_alone_ends_the_parse_with_success),
    NAOS_ADD_TEST(a_terminal_flag_of_a_subcommand_ends_the_parse_in_that_subcommand),
    NAOS_ADD_TEST(the_first_terminal_flag_ends_the_parse)
)

NAOS_SUITE(after,
    NAOS_ADD_TEST(the_arguments_after_a_terminal_flag_are_not_read),
    NAOS_ADD_TEST(a_terminal_flag_ends_a_bundle)
)

NAOS_SUITE(before,
    NAOS_ADD_TEST(what_was_read_before_a_terminal_flag_is_kept)
)

NAOS_SUITE(resolving,
    NAOS_ADD_TEST(default_values_are_not_given_after_a_terminal_flag)
)

NAOS_SUITE(errors,
    NAOS_ADD_TEST(a_parse_error_before_a_terminal_flag_is_reported),
    NAOS_ADD_TEST(a_value_for_a_terminal_flag_is_an_error),
    NAOS_ADD_TEST(a_terminal_flag_is_read_only_where_an_option_is),
    NAOS_ADD_TEST(a_terminal_flag_of_the_parent_is_unknown_in_a_subcommand)
)

NAOS_SUITE(without,
    NAOS_ADD_TEST(without_a_terminal_flag_the_command_line_is_read_and_resolved),
    NAOS_ADD_TEST(a_flag_that_is_no_longer_terminal_does_not_end_the_parse)
)

NAOS_SUITE(memory,
    NAOS_ADD_TEST(a_parse_that_a_terminal_flag_ends_cannot_allocate_and_leaves_nothing_allocated)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(where),
    NAOS_ADD_SUITE(after),
    NAOS_ADD_SUITE(before),
    NAOS_ADD_SUITE(resolving),
    NAOS_ADD_SUITE(errors),
    NAOS_ADD_SUITE(without),
    NAOS_ADD_SUITE(memory)
)
