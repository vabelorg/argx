/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * Tests of the public parsing API for list options, ARGX_OPTION_LIST:
 * argx_parse() of <argx/parse.h>, read back with the argx_result_*()
 * functions that take an index.
 *
 * The tests cover the order of the values across every form an option can
 * be given in, bundles included; the split at a separator and every kind of
 * empty item; values of every kind of value type and an item that its value
 * type rejects; default items, which a list that is given replaces;
 * required list options; the sources; the indexes of the arguments, for
 * options and positionals; the readers with an index on an option with a
 * value; global list options; terminal flags; and the cleanup of the values
 * and of the memory, also when an allocation fails.
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
 * The end of every test: the test allocator has no live block and saw no
 * violation of the allocator contract.
 */
static void check_clean(void)
{
    NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), 0);
    NAOS_CHECK_UINT_EQ(argx_test_allocator_violations(), 0);
}

/* Installs the test allocator and creates the root of a schema. */
static argx_command_t *create_root(void)
{
    argx_command_t *root = NULL;

    argx_test_allocator_install();
    NAOS_REQUIRE_TRUE(argx_command_create("tool", &root) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(root != NULL);
    return root;
}

static argx_command_t *add_subcommand(argx_command_t *command, const char *name)
{
    argx_command_t *subcommand = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_subcommand(command, name, &subcommand) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(subcommand != NULL);
    return subcommand;
}

/*
 * Adds a list option with the separator separator, '\0' for none, to
 * command.
 */
static argx_option_t *add_list(argx_command_t *command, char short_name, const char *long_name,
                               const argx_type_t *type, char separator)
{
    argx_option_t *option = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_list(command, short_name, long_name, type, &option) ==
                      ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(option != NULL);
    NAOS_REQUIRE_TRUE(argx_option_set_separator(option, separator) == ARGX_ERROR_NONE);
    return option;
}

static argx_option_t *add_flag(argx_command_t *command, char short_name, const char *long_name)
{
    argx_option_t *option = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_flag(command, short_name, long_name, &option) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(option != NULL);
    return option;
}

static argx_positional_t *add_positional(argx_command_t *command, const char *name, argx_arity_t arity)
{
    argx_positional_t *positional = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_positional(command, name, argx_type_string(), arity,
                                                  &positional) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(positional != NULL);
    return positional;
}

static void add_default_item(argx_option_t *option, const char *value)
{
    NAOS_REQUIRE_TRUE(argx_option_add_default(option, value) == ARGX_ERROR_NONE);
}

static void validate(argx_command_t *root)
{
    NAOS_REQUIRE_TRUE(argx_command_validate(root, NULL, NULL, NULL) == ARGX_ERROR_NONE);
}

/* Destroys the schema and ends the test: nothing stays allocated. */
static void destroy_root(argx_command_t *root)
{
    argx_command_destroy(root);
    argx_test_allocator_uninstall();
    check_clean();
}

/*
 * Parses argv, which must match the schema. Returns the result, which the
 * caller destroys.
 */
static argx_result_t *parse_ok(const argx_command_t *root, int argc, char *const *argv)
{
    argx_result_t *result = NULL;

    NAOS_REQUIRE_TRUE(argx_parse(root, argc, argv, &result) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(result != NULL);
    NAOS_CHECK_INT_EQ(argx_result_error_kind(result), ARGX_PARSE_ERROR_NONE);
    return result;
}

/*
 * Parses argv, which must have a parse error of the kind with the message.
 * Returns the result, which the caller destroys.
 */
static argx_result_t *parse_bad(const argx_command_t *root, int argc, char *const *argv,
                                argx_parse_error_kind_t kind, const char *message)
{
    argx_result_t *result = NULL;

    NAOS_REQUIRE_TRUE(argx_parse(root, argc, argv, &result) == ARGX_ERROR_INVALID_COMMAND_LINE);
    NAOS_REQUIRE_TRUE(result != NULL);
    NAOS_CHECK_INT_EQ(argx_result_error_kind(result), kind);
    NAOS_REQUIRE_TRUE(argx_result_error_message(result) != NULL);
    NAOS_CHECK_STR_EQ(argx_result_error_message(result), message);
    return result;
}

/*
 * Checks that option, a list of strings, has exactly the count items of
 * expected, given in the arguments of indexes, with the source.
 */
static void check_strings(const argx_result_t *result, const argx_option_t *option,
                          const char *const *expected, const size_t *indexes, size_t count,
                          argx_source_t source)
{
    const char *value = NULL;
    size_t index;

    NAOS_CHECK_INT_EQ(argx_result_option_source(result, option), source);
    NAOS_REQUIRE_TRUE(argx_result_option_value_count(result, option) == count);
    for (index = 0; index < count; ++index)
    {
        const void *area = argx_result_option_value_at(result, option, index);

        NAOS_REQUIRE_TRUE(argx_result_option_string_at(result, option, index, &value));
        NAOS_REQUIRE_TRUE(value != NULL);
        NAOS_CHECK_STR_EQ(value, expected[index]);
        NAOS_REQUIRE_TRUE(area != NULL);
        NAOS_CHECK_TRUE(*(const char *const *)area == value);
        NAOS_CHECK_UINT_EQ(argx_result_option_arg_index(result, option, index), indexes[index]);
    }

    /* Past the last item there is none, and *out stays as it was. */
    value = "unchanged";
    NAOS_CHECK_FALSE(argx_result_option_string_at(result, option, count, &value));
    NAOS_CHECK_STR_EQ(value, "unchanged");
    NAOS_CHECK_TRUE(argx_result_option_value_at(result, option, count) == NULL);
    NAOS_CHECK_UINT_EQ(argx_result_option_arg_index(result, option, count), 0);
}

/* ---- Collecting the values ---------------------------------------------- */

NAOS_TEST(every_form_of_an_option_adds_a_value_in_command_line_order)
{
    static const char *const expected[] = { "a", "b", "c", "d", "e", "f", "-x", "" };
    static const size_t indexes[] = { 1, 3, 4, 6, 7, 8, 10, 11 };
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), '\0');
    argx_option_t *verbose = add_flag(root, 'v', "verbose");
    char *argv[] = { "tool", "-ka", "-k", "b", "--keys=c", "--keys", "d", "-vke", "-vkf",
                     "--keys", "-x", "--keys=" };
    argx_result_t *result;

    validate(root);
    result = parse_ok(root, ARGC(argv), argv);
    check_strings(result, keys, expected, indexes, 8, ARGX_SOURCE_COMMAND_LINE);
    NAOS_CHECK_TRUE(argx_result_flag(result, verbose));
    argx_result_destroy(result);

    destroy_root(root);
}

NAOS_TEST(a_value_is_split_at_the_separator_and_its_items_share_the_index)
{
    static const char *const expected[] = { "a", "b", "c", "d", "e", " f ", "g" };
    static const size_t indexes[] = { 2, 2, 2, 3, 3, 5, 5 };
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ',');
    argx_positional_t *files = add_positional(root, "file", ARGX_ARITY_ZERO_OR_MORE);
    char *argv[] = { "tool", "--keys", "a,b,c", "-kd,e", "x.md", "--keys= f ,g" };
    argx_result_t *result;
    const char *file = NULL;

    validate(root);
    result = parse_ok(root, ARGC(argv), argv);
    check_strings(result, keys, expected, indexes, 7, ARGX_SOURCE_COMMAND_LINE);

    /* The arguments themselves are not changed by the split. */
    NAOS_CHECK_STR_EQ(argv[2], "a,b,c");

    /* The index of a positional value tells where it was among the options. */
    NAOS_REQUIRE_TRUE(argx_result_positional_string(result, files, 0, &file));
    NAOS_CHECK_STR_EQ(file, "x.md");
    NAOS_CHECK_UINT_EQ(argx_result_positional_arg_index(result, files, 0), 4);
    NAOS_CHECK_UINT_EQ(argx_result_positional_arg_index(result, files, 1), 0);
    argx_result_destroy(result);

    destroy_root(root);
}

/*
 * Without a separator, a value is one item, whatever it holds, and an empty
 * value is passed to the value type like any other.
 */
NAOS_TEST(without_a_separator_a_value_is_one_item)
{
    static const char *const expected[] = { "a,,b", "", "," };
    static const size_t indexes[] = { 2, 3, 5 };
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), '\0');
    argx_option_t *nums = add_list(root, 'n', "num", argx_type_int(), '\0');
    char *argv[] = { "tool", "-k", "a,,b", "--keys=", "-k", "," };
    char *empty_number[] = { "tool", "-n", "" };
    argx_result_t *result;

    validate(root);
    result = parse_ok(root, ARGC(argv), argv);
    check_strings(result, keys, expected, indexes, 3, ARGX_SOURCE_COMMAND_LINE);
    argx_result_destroy(result);

    result = parse_bad(root, ARGC(empty_number), empty_number, ARGX_PARSE_ERROR_INVALID_VALUE,
                       "invalid value '' for option '-n' (expected integer)");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == nums);
    argx_result_destroy(result);

    destroy_root(root);
}

/* ---- Empty items ---------------------------------------------------------- */

/*
 * Parses argv, whose last argument holds a value of --keys with an empty
 * item, and checks the details of the parse error. value is the whole
 * value, and short_name the letter for a bundle.
 */
static void check_empty_item(const argx_command_t *root, const argx_option_t *keys, int argc,
                             char *const *argv, const char *message, const char *value,
                             char short_name)
{
    argx_result_t *result = parse_bad(root, argc, argv, ARGX_PARSE_ERROR_EMPTY_ITEM, message);
    const size_t last = (size_t)argc - 1;

    NAOS_CHECK_INT_EQ(argx_result_error_source(result), ARGX_SOURCE_COMMAND_LINE);
    NAOS_CHECK_UINT_EQ(argx_result_error_index(result), last);
    NAOS_REQUIRE_TRUE(argx_result_error_argument(result) != NULL);
    NAOS_CHECK_STR_EQ(argx_result_error_argument(result), argv[last]);
    NAOS_REQUIRE_TRUE(argx_result_error_value(result) != NULL);
    NAOS_CHECK_STR_EQ(argx_result_error_value(result), value);
    NAOS_CHECK_TRUE(argx_result_error_option(result) == keys);
    NAOS_CHECK_TRUE(argx_result_error_positional(result) == NULL);
    NAOS_CHECK_TRUE(argx_result_error_command(result) == root);
    NAOS_CHECK_INT_EQ(argx_result_error_short_name(result), short_name);

    /* No item of the value is kept, and no default item is applied. */
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, keys), 1);
    argx_result_destroy(result);
}

NAOS_TEST(an_empty_item_is_a_parse_error)
{
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ',');
    char *two[] = { "tool", "-k", "x", "--keys", "a,,b" };
    char *leading[] = { "tool", "-kx", "--keys=,a" };
    char *trailing[] = { "tool", "-kx", "--keys=a," };
    char *empty[] = { "tool", "-kx", "--keys=" };
    char *separator[] = { "tool", "-kx", "--keys", "," };
    char *bundle[] = { "tool", "-kx", "-k,a" };

    add_default_item(keys, "default");
    validate(root);

    check_empty_item(root, keys, ARGC(two), two, "option '--keys' has an empty item in 'a,,b'", "a,,b",
                     '\0');
    check_empty_item(root, keys, ARGC(leading), leading, "option '--keys' has an empty item in ',a'",
                     ",a", '\0');
    check_empty_item(root, keys, ARGC(trailing), trailing, "option '--keys' has an empty item in 'a,'",
                     "a,", '\0');
    check_empty_item(root, keys, ARGC(empty), empty, "option '--keys' has an empty item in ''", "",
                     '\0');
    check_empty_item(root, keys, ARGC(separator), separator, "option '--keys' has an empty item in ','",
                     ",", '\0');
    check_empty_item(root, keys, ARGC(bundle), bundle, "option '-k' has an empty item in ',a'", ",a",
                     'k');

    destroy_root(root);
}

/*
 * A parse error about a global list option given below its command names
 * the command that was read, as for a missing value.
 */
NAOS_TEST(an_empty_item_of_a_global_option_names_the_current_command)
{
    argx_command_t *root = create_root();
    argx_command_t *build = add_subcommand(root, "build");
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ':');
    char *argv[] = { "tool", "build", "--keys", "a::b" };
    argx_result_t *result;

    NAOS_REQUIRE_TRUE(argx_option_set_global(keys, true) == ARGX_ERROR_NONE);
    validate(root);

    result = parse_bad(root, ARGC(argv), argv, ARGX_PARSE_ERROR_EMPTY_ITEM,
                       "option '--keys' has an empty item in 'a::b'");
    NAOS_CHECK_TRUE(argx_result_error_command(result) == build);
    NAOS_CHECK_TRUE(argx_result_error_option(result) == keys);
    NAOS_CHECK_UINT_EQ(argx_result_error_index(result), 3);
    argx_result_destroy(result);

    destroy_root(root);
}

/* ---- Value types ---------------------------------------------------------- */

NAOS_TEST(the_items_of_every_value_type_are_converted_one_by_one)
{
    static const char *const choices[] = { "fast", "small" };
    argx_command_t *root = create_root();
    argx_type_t *mode_type = NULL;
    argx_option_t *nums = add_list(root, 'n', "num", argx_type_int(), ',');
    argx_option_t *ratios = add_list(root, 'r', "ratio", argx_type_float(), ';');
    argx_option_t *modes;
    char *argv[] = { "tool", "-n1,-2", "--ratio=0.5;2", "--mode", "small|fast", "-n", "30" };
    argx_result_t *result;
    int64_t number = 0;
    double ratio = 0.0;
    size_t mode = 9;

    NAOS_REQUIRE_TRUE(argx_type_create_choice(choices, 2, &mode_type) == ARGX_ERROR_NONE);
    modes = add_list(root, 'm', "mode", mode_type, '|');
    validate(root);

    result = parse_ok(root, ARGC(argv), argv);
    NAOS_REQUIRE_TRUE(argx_result_option_value_count(result, nums) == 3);
    NAOS_REQUIRE_TRUE(argx_result_option_int_at(result, nums, 0, &number));
    NAOS_CHECK_TRUE(number == 1);
    NAOS_REQUIRE_TRUE(argx_result_option_int_at(result, nums, 1, &number));
    NAOS_CHECK_TRUE(number == -2);
    NAOS_REQUIRE_TRUE(argx_result_option_int_at(result, nums, 2, &number));
    NAOS_CHECK_TRUE(number == 30);
    NAOS_CHECK_TRUE(*(const int64_t *)argx_result_option_value_at(result, nums, 2) == 30);
    NAOS_CHECK_UINT_EQ(argx_result_option_arg_index(result, nums, 2), 6);
    number = 77;
    NAOS_CHECK_FALSE(argx_result_option_int_at(result, nums, 3, &number));
    NAOS_CHECK_TRUE(number == 77);

    NAOS_REQUIRE_TRUE(argx_result_option_value_count(result, ratios) == 2);
    NAOS_REQUIRE_TRUE(argx_result_option_float_at(result, ratios, 0, &ratio));
    NAOS_CHECK_TRUE(ratio == 0.5);
    NAOS_REQUIRE_TRUE(argx_result_option_float_at(result, ratios, 1, &ratio));
    NAOS_CHECK_TRUE(ratio == 2.0);

    NAOS_REQUIRE_TRUE(argx_result_option_value_count(result, modes) == 2);
    NAOS_REQUIRE_TRUE(argx_result_option_choice_at(result, modes, 0, &mode));
    NAOS_CHECK_UINT_EQ(mode, 1);
    NAOS_REQUIRE_TRUE(argx_result_option_choice_at(result, modes, 1, &mode));
    NAOS_CHECK_UINT_EQ(mode, 0);
    argx_result_destroy(result);

    argx_command_destroy(root);
    argx_type_destroy(mode_type);
    argx_test_allocator_uninstall();
    check_clean();
}

/*
 * An item that its value type rejects is an invalid value: the value of the
 * error is that item alone, which stays valid with the result, and the
 * argument is the whole one. The items before it in the same value are not
 * kept.
 */
NAOS_TEST(an_item_that_its_value_type_rejects_is_an_invalid_value)
{
    argx_command_t *root = create_root();
    argx_option_t *nums = add_list(root, 'n', "num", argx_type_int(), ',');
    char *argv[] = { "tool", "-n", "7", "--num=1,x2,3" };
    argx_result_t *result;
    int64_t number = 0;

    validate(root);
    result = parse_bad(root, ARGC(argv), argv, ARGX_PARSE_ERROR_INVALID_VALUE,
                       "invalid value 'x2' for option '--num' (expected integer)");
    NAOS_CHECK_INT_EQ(argx_result_error_source(result), ARGX_SOURCE_COMMAND_LINE);
    NAOS_CHECK_UINT_EQ(argx_result_error_index(result), 3);
    NAOS_REQUIRE_TRUE(argx_result_error_argument(result) != NULL);
    NAOS_CHECK_STR_EQ(argx_result_error_argument(result), "--num=1,x2,3");
    NAOS_REQUIRE_TRUE(argx_result_error_value(result) != NULL);
    NAOS_CHECK_STR_EQ(argx_result_error_value(result), "x2");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == nums);

    /* Only the value given before the failing one is held. */
    NAOS_REQUIRE_TRUE(argx_result_option_value_count(result, nums) == 1);
    NAOS_REQUIRE_TRUE(argx_result_option_int_at(result, nums, 0, &number));
    NAOS_CHECK_TRUE(number == 7);
    argx_result_destroy(result);

    destroy_root(root);
}

/* ---- Defaults, required and sources --------------------------------------- */

NAOS_TEST(the_default_items_are_used_only_when_the_option_is_not_given)
{
    static const char *const defaults[] = { "/usr/include", "a,b" };
    static const size_t default_indexes[] = { 0, 0 };
    static const char *const whole[] = { "x,y" };
    static const size_t whole_indexes[] = { 2 };
    static const char *const given[] = { "x", "y" };
    static const size_t given_indexes[] = { 3, 3 };
    argx_command_t *root = create_root();
    argx_option_t *include = add_list(root, 'I', "include", argx_type_string(), '\0');
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ',');
    char *none[] = { "tool" };
    char *some[] = { "tool", "-I", "x,y", "--keys=x,y" };
    argx_result_t *result;

    add_default_item(include, "/usr/include");
    add_default_item(include, "a,b");
    validate(root);

    /*
     * Not given: every default item, not split, and a list without one has
     * no item and no source.
     */
    result = parse_ok(root, ARGC(none), none);
    check_strings(result, include, defaults, default_indexes, 2, ARGX_SOURCE_DEFAULT);
    check_strings(result, keys, NULL, NULL, 0, ARGX_SOURCE_NONE);
    argx_result_destroy(result);

    /* Given: the defaults are replaced, not extended. */
    result = parse_ok(root, ARGC(some), some);
    check_strings(result, include, whole, whole_indexes, 1, ARGX_SOURCE_COMMAND_LINE);
    check_strings(result, keys, given, given_indexes, 2, ARGX_SOURCE_COMMAND_LINE);
    argx_result_destroy(result);

    destroy_root(root);
}

NAOS_TEST(a_required_list_option_must_be_given_at_least_once)
{
    static const char *const expected[] = { "a" };
    static const size_t indexes[] = { 2 };
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, '\0', "keys", argx_type_string(), ',');
    argx_option_t *nums = add_list(root, 'n', NULL, argx_type_int(), '\0');
    char *none[] = { "tool" };
    char *keys_only[] = { "tool", "--keys", "a" };
    char *both[] = { "tool", "--keys", "a", "-n1" };
    argx_result_t *result;

    NAOS_REQUIRE_TRUE(argx_option_set_required(keys, true) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_required(nums, true) == ARGX_ERROR_NONE);
    validate(root);

    /* The options are checked in the order they were added. */
    result = parse_bad(root, ARGC(none), none, ARGX_PARSE_ERROR_MISSING_OPTION,
                       "missing required option '--keys'");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == keys);
    argx_result_destroy(result);

    result = parse_bad(root, ARGC(keys_only), keys_only, ARGX_PARSE_ERROR_MISSING_OPTION,
                       "missing required option '-n'");
    NAOS_CHECK_TRUE(argx_result_error_option(result) == nums);
    argx_result_destroy(result);

    result = parse_ok(root, ARGC(both), both);
    check_strings(result, keys, expected, indexes, 1, ARGX_SOURCE_COMMAND_LINE);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, nums), 1);
    argx_result_destroy(result);

    destroy_root(root);
}

NAOS_TEST(a_list_option_of_a_command_that_was_not_selected_has_no_item)
{
    argx_command_t *root = create_root();
    argx_command_t *build = add_subcommand(root, "build");
    argx_command_t *test = add_subcommand(root, "test");
    argx_option_t *keys = add_list(build, 'k', "keys", argx_type_string(), ',');
    char *argv[] = { "tool", "test" };
    argx_result_t *result;

    add_default_item(keys, "a");
    NAOS_REQUIRE_TRUE(argx_option_set_required(keys, false) == ARGX_ERROR_NONE);
    validate(root);

    result = parse_ok(root, ARGC(argv), argv);
    NAOS_CHECK_TRUE(argx_result_command(result) == test);
    check_strings(result, keys, NULL, NULL, 0, ARGX_SOURCE_NONE);
    argx_result_destroy(result);

    destroy_root(root);
}

/*
 * A global list option collects its values from every command it is given
 * in, in command-line order, and its default items belong to its command.
 */
NAOS_TEST(a_global_list_option_collects_its_values_across_the_commands)
{
    static const char *const expected[] = { "a", "b", "c", "d" };
    static const size_t indexes[] = { 2, 4, 4, 6 };
    static const char *const defaults[] = { "z" };
    static const size_t default_indexes[] = { 0 };
    argx_command_t *root = create_root();
    argx_command_t *build = add_subcommand(root, "build");
    argx_command_t *now = add_subcommand(build, "now");
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ',');
    char *argv[] = { "tool", "-k", "a", "build", "--keys=b,c", "now", "-kd" };
    char *none[] = { "tool", "build", "now" };
    argx_result_t *result;

    NAOS_REQUIRE_TRUE(argx_option_set_global(keys, true) == ARGX_ERROR_NONE);
    add_default_item(keys, "z");
    validate(root);

    result = parse_ok(root, ARGC(argv), argv);
    NAOS_CHECK_TRUE(argx_result_command(result) == now);
    check_strings(result, keys, expected, indexes, 4, ARGX_SOURCE_COMMAND_LINE);
    argx_result_destroy(result);

    result = parse_ok(root, ARGC(none), none);
    check_strings(result, keys, defaults, default_indexes, 1, ARGX_SOURCE_DEFAULT);
    argx_result_destroy(result);

    destroy_root(root);
}

NAOS_TEST(a_terminal_flag_leaves_a_list_option_without_its_default_items)
{
    static const char *const expected[] = { "a", "b" };
    static const size_t indexes[] = { 1, 1 };
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ',');
    argx_option_t *help = add_flag(root, 'h', "help");
    char *argv[] = { "tool", "-ka,b", "--help", "-kc" };
    char *alone[] = { "tool", "-h" };
    argx_result_t *result;

    NAOS_REQUIRE_TRUE(argx_option_set_terminal(help, true) == ARGX_ERROR_NONE);
    add_default_item(keys, "z");
    validate(root);

    result = parse_ok(root, ARGC(argv), argv);
    check_strings(result, keys, expected, indexes, 2, ARGX_SOURCE_COMMAND_LINE);
    argx_result_destroy(result);

    result = parse_ok(root, ARGC(alone), alone);
    check_strings(result, keys, NULL, NULL, 0, ARGX_SOURCE_NONE);
    argx_result_destroy(result);

    destroy_root(root);
}

/* ---- Indexed readers on an option with a value ---------------------------- */

NAOS_TEST(an_option_with_a_value_reads_as_zero_or_one_item)
{
    argx_command_t *root = create_root();
    argx_option_t *output = NULL;
    argx_option_t *level = NULL;
    char *given[] = { "tool", "--output", "a.txt", "-o", "b.txt" };
    char *none[] = { "tool" };
    argx_result_t *result;
    const char *value = NULL;
    int64_t number = 0;

    NAOS_REQUIRE_TRUE(argx_command_add_value(root, 'o', "output", argx_type_string(), &output) ==
                      ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_command_add_value(root, 'l', "level", argx_type_int(), &level) ==
                      ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_default(level, "6") == ARGX_ERROR_NONE);
    validate(root);

    /* The value given last, in the argument that holds it. */
    result = parse_ok(root, ARGC(given), given);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, output), 1);
    NAOS_REQUIRE_TRUE(argx_result_option_string_at(result, output, 0, &value));
    NAOS_CHECK_STR_EQ(value, "b.txt");
    NAOS_CHECK_TRUE(argx_result_option_value_at(result, output, 0) == argx_result_option_value(result, output));
    NAOS_CHECK_UINT_EQ(argx_result_option_arg_index(result, output, 0), 4);
    NAOS_CHECK_FALSE(argx_result_option_string_at(result, output, 1, &value));
    NAOS_CHECK_TRUE(argx_result_option_value_at(result, output, 1) == NULL);
    NAOS_CHECK_UINT_EQ(argx_result_option_arg_index(result, output, 1), 0);

    /* A default value is the item at 0, with the index 0. */
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, level), 1);
    NAOS_REQUIRE_TRUE(argx_result_option_int_at(result, level, 0, &number));
    NAOS_CHECK_TRUE(number == 6);
    NAOS_CHECK_UINT_EQ(argx_result_option_arg_index(result, level, 0), 0);
    argx_result_destroy(result);

    result = parse_ok(root, ARGC(none), none);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, output), 0);
    value = "unchanged";
    NAOS_CHECK_FALSE(argx_result_option_string_at(result, output, 0, &value));
    NAOS_CHECK_STR_EQ(value, "unchanged");
    argx_result_destroy(result);

    destroy_root(root);
}

/* ---- Cleanup -------------------------------------------------------------- */

/*
 * A user-defined value type that counts its conversions and cleanups, and
 * takes every text except "bad" and, once late is set, "late".
 */
typedef struct tally
{
    size_t converted;
    size_t released;
    bool late;
} tally_t;

static argx_error_t tally_convert(const char *text, void *value, void *context)
{
    tally_t *tally = context;

    if ((strcmp(text, "bad") == 0) || (tally->late && (strcmp(text, "late") == 0)))
    {
        return ARGX_ERROR_INVALID_VALUE;
    }

    *(const char **)value = text;
    tally->converted++;
    return ARGX_ERROR_NONE;
}

static void tally_release(void *value, void *context)
{
    tally_t *tally = context;

    (void)value;
    tally->released++;
}

/*
 * Every item that was converted is cleaned up once, with the result or when
 * the items of its value are not kept, and a default item that fails at the
 * parse takes back the default values that were applied.
 */
NAOS_TEST(every_item_is_cleaned_up_once)
{
    tally_t tally = { 0, 0, false };
    argx_type_t *type = NULL;
    argx_command_t *root = create_root();
    argx_option_t *words;
    argx_option_t *late;
    char *good[] = { "tool", "-w", "a,b", "-wc", "--word=d,e,f" };
    char *bad[] = { "tool", "-w", "a,b", "-wc,d,bad,e" };
    char *none[] = { "tool" };
    argx_result_t *result;
    size_t before;

    NAOS_REQUIRE_TRUE(argx_type_create("word", sizeof(const char *), tally_convert, tally_release, &tally,
                                       &type) == ARGX_ERROR_NONE);
    words = add_list(root, 'w', "word", type, ',');
    late = add_list(root, 'l', "late", type, '\0');
    add_default_item(words, "x");
    add_default_item(late, "one");
    add_default_item(late, "late");
    validate(root);

    before = tally.converted;
    result = parse_ok(root, ARGC(good), good);
    NAOS_CHECK_UINT_EQ(tally.converted - before, 8);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, words), 6);
    argx_result_destroy(result);
    NAOS_CHECK_UINT_EQ(tally.released, tally.converted);

    /* "c" and "d" were converted, then cleaned up when "bad" failed. */
    before = tally.converted;
    result = parse_bad(root, ARGC(bad), bad, ARGX_PARSE_ERROR_INVALID_VALUE,
                       "invalid value 'bad' for option '-w' (expected word)");
    NAOS_CHECK_UINT_EQ(tally.converted - before, 4);
    NAOS_CHECK_UINT_EQ(tally.released, tally.converted - 2);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, words), 2);
    argx_result_destroy(result);
    NAOS_CHECK_UINT_EQ(tally.released, tally.converted);

    /*
     * A default item that the value type no longer takes: the parse error
     * leaves no default value applied.
     */
    tally.late = true;
    result = parse_bad(root, ARGC(none), none, ARGX_PARSE_ERROR_INVALID_VALUE,
                       "invalid default value 'late' for option '--late' (expected word)");
    NAOS_CHECK_INT_EQ(argx_result_error_source(result), ARGX_SOURCE_DEFAULT);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, words), 0);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, late), 0);
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, words), ARGX_SOURCE_NONE);
    NAOS_CHECK_INT_EQ(argx_result_option_source(result, late), ARGX_SOURCE_NONE);
    NAOS_CHECK_UINT_EQ(tally.released, tally.converted);
    argx_result_destroy(result);

    argx_command_destroy(root);
    argx_type_destroy(type);
    argx_test_allocator_uninstall();
    NAOS_CHECK_UINT_EQ(tally.released, tally.converted);
    check_clean();
}

/*
 * Each allocation of a parse with list options fails in turn: the parse
 * returns no result and leaves nothing allocated, and then succeeds again.
 */
NAOS_TEST(a_parse_that_cannot_allocate_returns_no_result_and_leaves_nothing_allocated)
{
    static const char *const keys_expected[] = { "a", "b", "c", "d", "e", "f" };
    static const size_t keys_indexes[] = { 2, 2, 2, 3, 3, 3 };
    argx_command_t *root = create_root();
    argx_option_t *keys = add_list(root, 'k', "keys", argx_type_string(), ',');
    argx_option_t *nums = add_list(root, 'n', "num", argx_type_int(), '\0');
    argx_option_t *dirs = add_list(root, 'I', "include", argx_type_string(), '\0');
    char *argv[] = { "tool", "--keys", "a,b,c", "-kd,e,f", "-n1", "-n2", "-n3", "-n4", "-n5" };
    size_t live;
    size_t before;
    size_t requests;
    size_t nth;
    argx_result_t *result;

    add_default_item(dirs, "/usr/include");
    add_default_item(dirs, "/opt/include");
    validate(root);
    live = argx_test_allocator_live();

    before = argx_test_allocator_requests();
    result = parse_ok(root, ARGC(argv), argv);
    requests = argx_test_allocator_requests() - before;
    argx_result_destroy(result);
    NAOS_REQUIRE_TRUE(requests > 0);

    for (nth = 1; nth <= requests; nth++)
    {
        argx_error_t error;

        result = NULL;
        argx_test_allocator_fail_at(argx_test_allocator_requests() + nth);
        error = argx_parse(root, ARGC(argv), argv, &result);
        argx_test_allocator_fail_at(0);

        NAOS_CHECK_INT_EQ(error, ARGX_ERROR_OUT_OF_MEMORY);
        NAOS_CHECK_TRUE(result == NULL);
        NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), live);
    }

    result = parse_ok(root, ARGC(argv), argv);
    check_strings(result, keys, keys_expected, keys_indexes, 6, ARGX_SOURCE_COMMAND_LINE);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, nums), 5);
    NAOS_CHECK_UINT_EQ(argx_result_option_value_count(result, dirs), 2);
    argx_result_destroy(result);
    NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), live);

    destroy_root(root);
}

NAOS_SUITE(collect,
    NAOS_ADD_TEST(every_form_of_an_option_adds_a_value_in_command_line_order),
    NAOS_ADD_TEST(a_value_is_split_at_the_separator_and_its_items_share_the_index),
    NAOS_ADD_TEST(without_a_separator_a_value_is_one_item)
)

NAOS_SUITE(empty_items,
    NAOS_ADD_TEST(an_empty_item_is_a_parse_error),
    NAOS_ADD_TEST(an_empty_item_of_a_global_option_names_the_current_command)
)

NAOS_SUITE(value_types,
    NAOS_ADD_TEST(the_items_of_every_value_type_are_converted_one_by_one),
    NAOS_ADD_TEST(an_item_that_its_value_type_rejects_is_an_invalid_value)
)

NAOS_SUITE(resolve,
    NAOS_ADD_TEST(the_default_items_are_used_only_when_the_option_is_not_given),
    NAOS_ADD_TEST(a_required_list_option_must_be_given_at_least_once),
    NAOS_ADD_TEST(a_list_option_of_a_command_that_was_not_selected_has_no_item),
    NAOS_ADD_TEST(a_global_list_option_collects_its_values_across_the_commands),
    NAOS_ADD_TEST(a_terminal_flag_leaves_a_list_option_without_its_default_items)
)

NAOS_SUITE(readers,
    NAOS_ADD_TEST(an_option_with_a_value_reads_as_zero_or_one_item)
)

NAOS_SUITE(memory,
    NAOS_ADD_TEST(every_item_is_cleaned_up_once),
    NAOS_ADD_TEST(a_parse_that_cannot_allocate_returns_no_result_and_leaves_nothing_allocated)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(collect),
    NAOS_ADD_SUITE(empty_items),
    NAOS_ADD_SUITE(value_types),
    NAOS_ADD_SUITE(resolve),
    NAOS_ADD_SUITE(readers),
    NAOS_ADD_SUITE(memory)
)
