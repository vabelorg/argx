/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * Tests of the public parsing API for global options: argx_parse() of
 * <argx/parse.h> with options made global by argx_option_set_global(),
 * read back with the argx_result_*() functions.
 *
 * A global option is known on its command and on every descendant of it.
 * The tests cover where it may be given (on its command, below it, on
 * several levels at once), where it is not known (above its command, in a
 * sibling), how it is read (one handle, counts that add up, the last value
 * that wins), what belongs to its command (the required check and the
 * default value), the parse errors about it, a terminal global flag, and
 * the digit short names that make an argument a bundle.
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
#include <string.h>

/* The number of arguments of an argv array. */
#define ARGC(argv) ((int)(sizeof(argv) / sizeof((argv)[0])))

/*
 * The schema of the tests:
 *
 *   app [-h|--help] [-v|--verbose]... [--[no-]color] [-c|--config STRING]
 *       [--local]
 *     run [-q|--quiet] [-j|--jobs STRING, "1"] [-t|--target STRING]
 *       now [-f|--force] <FILE>
 *     build [-n NUMBER]
 *
 * --help (terminal), --verbose, --color (on at first) and --config are
 * global on app; --local is not. --quiet and --target are global on run,
 * and --target is required. --jobs is not global. The subcommands of app
 * are required.
 */
typedef struct fixture
{
    argx_command_t *root;
    argx_option_t *help;
    argx_option_t *verbose;
    argx_option_t *color;
    argx_option_t *config;
    argx_option_t *local;
    argx_command_t *run;
    argx_option_t *quiet;
    argx_option_t *jobs;
    argx_option_t *target;
    argx_command_t *now;
    argx_option_t *force;
    argx_positional_t *file;
    argx_command_t *build;
    argx_positional_t *number;
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

/* Makes option global. */
static void make_global(argx_option_t *option)
{
    NAOS_REQUIRE_TRUE(argx_option_set_global(option, true) == ARGX_ERROR_NONE);
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
    NAOS_REQUIRE_TRUE(argx_command_create("app", &root) == ARGX_ERROR_NONE);
    fixture->root = root;

    NAOS_REQUIRE_TRUE(argx_command_add_flag(root, 'h', "help", &fixture->help) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_terminal(fixture->help, true) == ARGX_ERROR_NONE);
    make_global(fixture->help);
    NAOS_REQUIRE_TRUE(argx_command_add_count(root, 'v', "verbose", &fixture->verbose) == ARGX_ERROR_NONE);
    make_global(fixture->verbose);
    NAOS_REQUIRE_TRUE(argx_command_add_negatable(root, '\0', "color", &fixture->color) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_initial(fixture->color, true) == ARGX_ERROR_NONE);
    make_global(fixture->color);
    fixture->config = add_string(root, 'c', "config");
    make_global(fixture->config);
    NAOS_REQUIRE_TRUE(argx_command_add_flag(root, '\0', "local", &fixture->local) == ARGX_ERROR_NONE);

    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(root, "run", &fixture->run) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_flag(fixture->run, 'q', "quiet", &fixture->quiet) == ARGX_ERROR_NONE);
    make_global(fixture->quiet);
    fixture->jobs = add_string(fixture->run, 'j', "jobs");
    NAOS_REQUIRE_TRUE(argx_option_set_default(fixture->jobs, "1") == ARGX_ERROR_NONE);
    fixture->target = add_string(fixture->run, 't', "target");
    NAOS_REQUIRE_TRUE(argx_option_set_required(fixture->target, true) == ARGX_ERROR_NONE);
    make_global(fixture->target);

    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(fixture->run, "now", &fixture->now) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_set_subcommand_optional(fixture->run, true) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_flag(fixture->now, 'f', "force", &fixture->force) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_positional(fixture->now, "FILE", argx_type_string(),
                                                  ARGX_ARITY_REQUIRED, &fixture->file) == ARGX_ERROR_NONE);

    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(root, "build", &fixture->build) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_positional(fixture->build, "NUMBER", argx_type_string(),
                                                  ARGX_ARITY_OPTIONAL, &fixture->number) == ARGX_ERROR_NONE);

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
 * Parses argv, which must succeed and end in command, without a terminal
 * flag. Returns the result, which the caller destroys.
 */
static argx_result_t *parse_good(const fixture_t *fixture, int argc, char *const *argv,
                                 const argx_command_t *command)
{
    argx_result_t *result = NULL;

    NAOS_REQUIRE_TRUE(argx_parse(fixture->root, argc, argv, &result) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(result != NULL);
    NAOS_CHECK_INT_EQ(argx_result_error_kind(result), ARGX_PARSE_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_command(result) == command);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == NULL);
    return result;
}

/*
 * Parses argv, which must have a parse error of the kind, at index, found
 * while command was read, with the message. Returns the result, which the
 * caller destroys.
 */
static argx_result_t *parse_bad(const fixture_t *fixture, int argc, char *const *argv,
                                argx_parse_error_kind_t kind, size_t index,
                                const argx_command_t *command, const char *message)
{
    argx_result_t *result = NULL;

    NAOS_REQUIRE_TRUE(argx_parse(fixture->root, argc, argv, &result) == ARGX_ERROR_INVALID_COMMAND_LINE);
    NAOS_REQUIRE_TRUE(result != NULL);
    NAOS_CHECK_INT_EQ(argx_result_error_kind(result), kind);
    NAOS_CHECK_UINT_EQ(argx_result_error_index(result), index);
    NAOS_CHECK_TRUE(argx_result_error_command(result) == command);
    NAOS_REQUIRE_TRUE(argx_result_error_message(result) != NULL);
    NAOS_CHECK_STR_EQ(argx_result_error_message(result), message);
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

/* ---- Where a global option is known --------------------------------------- */

/* A global option is given on its command, as any option is. */
NAOS_TEST(a_global_option_is_known_on_its_command)
{
    fixture_t fixture;
    char *argv[] = { "app", "-v", "--config", "a.conf", "--no-color", "build" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_good(&fixture, ARGC(argv), argv, fixture.build);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 1);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.config), "a.conf");
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.color));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * After the name of a subcommand, at any depth, a global option of an
 * ancestor is known, by its long name, its short name, its negated name
 * and in a bundle with an option of the current command.
 */
NAOS_TEST(a_global_option_is_known_on_every_descendant)
{
    fixture_t fixture;
    char *in_child[] = { "app", "build", "--verbose", "-c", "b.conf", "--no-color" };
    char *in_grandchild[] = { "app", "run", "now", "-fv", "--config=c.conf", "-t", "x", "-q", "file" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_good(&fixture, ARGC(in_child), in_child, fixture.build);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 1);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.config), "b.conf");
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.color));
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, fixture.config), ARGX_SOURCE_COMMAND_LINE);
    argx_result_destroy(result);

    /* The globals of app and of run, two and one levels up. */
    result = parse_good(&fixture, ARGC(in_grandchild), in_grandchild, fixture.now);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.force));
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 1);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.config), "c.conf");
    NAOS_CHECK_STR_EQ(string_of(result, fixture.target), "x");
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.quiet));
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.color));
    NAOS_CHECK_UINT_EQ(argx_result_positional_count(result, fixture.file), 1);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* An option of an ancestor that is not global is not known below it. */
NAOS_TEST(an_option_that_is_not_global_is_not_known_below_its_command)
{
    fixture_t fixture;
    char *local[] = { "app", "build", "--local" };
    char *jobs[] = { "app", "run", "-t", "x", "now", "-j", "2", "file" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(local), local, ARGX_PARSE_ERROR_UNKNOWN_OPTION, 2, fixture.build,
                       "unknown option '--local'");
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(jobs), jobs, ARGX_PARSE_ERROR_UNKNOWN_OPTION, 5, fixture.now,
                       "unknown option '-j'");
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * A global option of a subcommand is not known on its parent, before its
 * name, or in a sibling.
 */
NAOS_TEST(a_global_option_is_not_known_above_its_command_or_in_a_sibling)
{
    fixture_t fixture;
    char *above[] = { "app", "--quiet", "run" };
    char *sibling[] = { "app", "build", "-q" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(above), above, ARGX_PARSE_ERROR_UNKNOWN_OPTION, 1, fixture.root,
                       "unknown option '--quiet'");
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(sibling), sibling, ARGX_PARSE_ERROR_UNKNOWN_OPTION, 2, fixture.build,
                       "unknown option '-q'");
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- How a global option is read ------------------------------------------- */

/*
 * One option, one handle: wherever it is given, it is read with the handle
 * that the addition wrote, and a count adds up across the commands.
 */
NAOS_TEST(a_count_given_on_several_commands_adds_up)
{
    fixture_t fixture;
    char *argv[] = { "app", "-v", "run", "-vv", "-t", "x", "now", "--verbose", "file" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_good(&fixture, ARGC(argv), argv, fixture.now);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 4);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* The value given last wins, as for an option given twice on one command. */
NAOS_TEST(the_value_given_last_wins_across_commands)
{
    fixture_t fixture;
    char *value[] = { "app", "--config", "first", "run", "-t", "x", "--config", "second" };
    char *state[] = { "app", "--no-color", "run", "-t", "x", "--color" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_good(&fixture, ARGC(value), value, fixture.run);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.config), "second");
    argx_result_destroy(result);

    result = parse_good(&fixture, ARGC(state), state, fixture.run);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.color));
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * The required check belongs to the command that defines the option: a
 * required global option may be given below it, and is missing in its
 * command when it is not given at all.
 */
NAOS_TEST(a_required_global_option_is_checked_in_its_command)
{
    fixture_t fixture;
    char *below[] = { "app", "run", "now", "--target", "x", "file" };
    char *missing[] = { "app", "run", "now", "file" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_good(&fixture, ARGC(below), below, fixture.now);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.target), "x");
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(missing), missing, ARGX_PARSE_ERROR_MISSING_OPTION, 0, fixture.run,
                       "missing required option '--target'");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == fixture.target);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * A global option without a value gets its default value, and its initial
 * state, as any option of its command does.
 */
NAOS_TEST(a_global_option_that_is_not_given_reads_as_not_given)
{
    fixture_t fixture;
    char *argv[] = { "app", "run", "-t", "x", "now", "file" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_good(&fixture, ARGC(argv), argv, fixture.now);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 0);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.color));
    NAOS_CHECK_FALSE(argx_result_flag(result, fixture.quiet));
    NAOS_CHECK_TRUE(argx_result_option_value(result, fixture.config) == NULL);
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, fixture.config), ARGX_SOURCE_NONE);
    NAOS_CHECK_STR_EQ(string_of(result, fixture.jobs), "1");
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- Parse errors and terminal flags ---------------------------------------- */

/*
 * A parse error about a global option given below its command is found
 * while the current command is read: argx_result_error_command() is that
 * command, and argx_result_error_option() is the global option.
 */
NAOS_TEST(a_parse_error_about_a_global_option_names_the_current_command)
{
    fixture_t fixture;
    char *missing[] = { "app", "build", "--config" };
    char *unexpected[] = { "app", "build", "--verbose=2" };
    argx_result_t *result;

    set_up(&fixture);

    result = parse_bad(&fixture, ARGC(missing), missing, ARGX_PARSE_ERROR_MISSING_VALUE, 2, fixture.build,
                       "option '--config' requires a value");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == fixture.config);
    argx_result_destroy(result);

    result = parse_bad(&fixture, ARGC(unexpected), unexpected, ARGX_PARSE_ERROR_UNEXPECTED_VALUE, 2,
                       fixture.build, "option '--verbose' does not take a value");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == fixture.verbose);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/*
 * A global terminal flag ends the parse wherever it is given: the result
 * names the command that was current, so the program prints that
 * command's help. What is required, below or above, is not checked.
 */
NAOS_TEST(a_global_terminal_flag_ends_the_parse_in_the_current_command)
{
    fixture_t fixture;
    char *in_child[] = { "app", "run", "--help" };
    char *in_grandchild[] = { "app", "run", "now", "-vh", "--bogus" };
    argx_result_t *result = NULL;

    set_up(&fixture);

    NAOS_REQUIRE_TRUE(argx_parse(fixture.root, ARGC(in_child), in_child, &result) == ARGX_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_command(result) == fixture.run);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == fixture.help);
    NAOS_CHECK_TRUE(argx_result_flag(result, fixture.help));
    NAOS_CHECK_TRUE(argx_result_option_value(result, fixture.target) == NULL);
    argx_result_destroy(result);

    result = NULL;
    NAOS_REQUIRE_TRUE(argx_parse(fixture.root, ARGC(in_grandchild), in_grandchild, &result) == ARGX_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_command(result) == fixture.now);
    NAOS_CHECK_TRUE(argx_result_terminal_option(result) == fixture.help);
    NAOS_CHECK_UINT_EQ(argx_result_count(result, fixture.verbose), 1);
    NAOS_CHECK_UINT_EQ(argx_result_positional_count(result, fixture.file), 0);
    argx_result_destroy(result);

    tear_down(&fixture);
}

/* ---- Digit short names -------------------------------------------------------- */

/*
 * A global option whose short name is a digit makes an argument that
 * looks like a negative number a bundle on every descendant. Without it,
 * the argument is a value.
 */
NAOS_TEST(a_global_digit_short_name_makes_a_bundle_on_every_descendant)
{
    argx_command_t *root = NULL;
    argx_command_t *build = NULL;
    argx_option_t *one = NULL;
    argx_positional_t *number = NULL;
    argx_result_t *result = NULL;
    char *argv[] = { "app", "build", "-1" };
    const char *text = NULL;

    argx_test_allocator_install();
    NAOS_REQUIRE_TRUE(argx_command_create("app", &root) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_flag(root, '1', "one", &one) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(root, "build", &build) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_positional(build, "NUMBER", argx_type_string(), ARGX_ARITY_OPTIONAL,
                                                  &number) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_validate(root, NULL, NULL, NULL) == ARGX_ERROR_NONE);

    /* Not global: "-1" is the value of NUMBER. */
    NAOS_REQUIRE_TRUE(argx_parse(root, ARGC(argv), argv, &result) == ARGX_ERROR_NONE);
    NAOS_CHECK_FALSE(argx_result_flag(result, one));
    NAOS_REQUIRE_TRUE(argx_result_positional_string(result, number, 0, &text));
    NAOS_CHECK_STR_EQ(text, "-1");
    argx_result_destroy(result);

    /* Global: "-1" is the flag. */
    make_global(one);
    NAOS_REQUIRE_TRUE(argx_command_validate(root, NULL, NULL, NULL) == ARGX_ERROR_NONE);
    result = NULL;
    NAOS_REQUIRE_TRUE(argx_parse(root, ARGC(argv), argv, &result) == ARGX_ERROR_NONE);
    NAOS_CHECK_TRUE(argx_result_flag(result, one));
    NAOS_CHECK_UINT_EQ(argx_result_positional_count(result, number), 0);
    argx_result_destroy(result);

    argx_command_destroy(root);
    argx_test_allocator_uninstall();
    check_clean();
}

NAOS_SUITE(where,
    NAOS_ADD_TEST(a_global_option_is_known_on_its_command),
    NAOS_ADD_TEST(a_global_option_is_known_on_every_descendant),
    NAOS_ADD_TEST(an_option_that_is_not_global_is_not_known_below_its_command),
    NAOS_ADD_TEST(a_global_option_is_not_known_above_its_command_or_in_a_sibling)
)

NAOS_SUITE(reading,
    NAOS_ADD_TEST(a_count_given_on_several_commands_adds_up),
    NAOS_ADD_TEST(the_value_given_last_wins_across_commands),
    NAOS_ADD_TEST(a_required_global_option_is_checked_in_its_command),
    NAOS_ADD_TEST(a_global_option_that_is_not_given_reads_as_not_given)
)

NAOS_SUITE(errors,
    NAOS_ADD_TEST(a_parse_error_about_a_global_option_names_the_current_command),
    NAOS_ADD_TEST(a_global_terminal_flag_ends_the_parse_in_the_current_command)
)

NAOS_SUITE(digits,
    NAOS_ADD_TEST(a_global_digit_short_name_makes_a_bundle_on_every_descendant)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(where),
    NAOS_ADD_SUITE(reading),
    NAOS_ADD_SUITE(errors),
    NAOS_ADD_SUITE(digits)
)
