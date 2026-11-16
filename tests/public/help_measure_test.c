/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * Tests of the measure function of the help options,
 * argx_help_options_set_measure() of <argx/help.h>: the description
 * column, the breaking of descriptions and of the usage line follow the
 * widths it returns, it is never passed a newline or nothing, NULL
 * restores one byte per column, and huge or changing widths keep the
 * generation defined.
 *
 * The measure function of most tests counts an ASCII byte as 1 column and
 * a byte that starts a UTF-8 sequence as 2, so a kana or a kanji (3
 * bytes) is 2 columns, as on most terminals. It needs no Unicode data, so
 * the results do not depend on the platform.
 *
 * Every test uses the test allocator ("../support/test_allocator.h"). A
 * test starts with an install, destroys what it created, sets the default
 * allocator back before it ends, and ends with check_clean().
 */
#include <argx/argx.h>

#include "../support/test_allocator.h"

#include <naos/naos.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The size of the text that the sink holds. */
#define SINK_SIZE ((size_t)2048)

/* What the write function of the tests has received. */
typedef struct sink
{
    char text[SINK_SIZE]; /* the parts, joined in the order of the calls */
    size_t length;        /* the number of bytes of text */
} sink_t;

static bool sink_write(void *user_data, const char *text, size_t length)
{
    sink_t *sink = user_data;

    if ((text == NULL) || (length == 0) || (length > (SINK_SIZE - sink->length)))
    {
        return false;
    }

    memcpy(sink->text + sink->length, text, length);
    sink->length += length;
    return true;
}

/*
 * What the measure function of the tests has received, and how it
 * measures.
 */
typedef enum gauge_mode
{
    GAUGE_UTF8,    /* ASCII: 1 column; a byte that starts a sequence: 2 */
    GAUGE_HUGE,    /* the greatest size_t value but one, for any text */
    GAUGE_CHANGING /* a different width at each call */
} gauge_mode_t;

typedef struct gauge
{
    gauge_mode_t mode;
    size_t calls;     /* the number of calls */
    size_t bad_calls; /* the calls with no text, the length 0 or a '\n' */
} gauge_t;

static void gauge_init(gauge_t *gauge, gauge_mode_t mode)
{
    memset(gauge, 0, sizeof *gauge);
    gauge->mode = mode;
}

static size_t gauge_measure(void *user_data, const char *text, size_t length)
{
    gauge_t *gauge = user_data;
    size_t width = 0;
    size_t i;

    ++gauge->calls;
    if ((text == NULL) || (length == 0) || (memchr(text, '\n', length) != NULL))
    {
        ++gauge->bad_calls;
        return 1;
    }

    switch (gauge->mode)
    {
    case GAUGE_HUGE:
        return (size_t)-1 - 1;
    case GAUGE_CHANGING:
        return (gauge->calls * 7) % 50;
    case GAUGE_UTF8:
        break;
    }

    for (i = 0; i < length; ++i)
    {
        unsigned char byte = (unsigned char)text[i];

        if (byte < 0x80)
        {
            width += 1;
        }
        else if (byte >= 0xC0)
        {
            width += 2;
        }
    }

    return width;
}

/*
 * The end of every test: the test allocator has no live block and saw no
 * violation of the allocator contract.
 */
static void check_clean(void)
{
    NAOS_CHECK_UINT_EQ(argx_test_allocator_live(), 0);
    NAOS_CHECK_UINT_EQ(argx_test_allocator_violations(), 0);
}

/* Installs the test allocator and creates a root with the name "tool". */
static argx_command_t *create_root(void)
{
    argx_command_t *root = NULL;

    argx_test_allocator_install();
    NAOS_REQUIRE_TRUE(argx_command_create("tool", &root) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(root != NULL);
    return root;
}

/* Destroys root and ends the test: nothing stays allocated. */
static void destroy_root(argx_command_t *root)
{
    argx_command_destroy(root);
    argx_test_allocator_uninstall();
    check_clean();
}

/* Validates the tree of root; it must have no fault. */
static void validate(argx_command_t *root)
{
    NAOS_REQUIRE_TRUE(argx_command_validate(root, NULL, NULL, NULL) == ARGX_ERROR_NONE);
}

/*
 * Creates help options with the width given and, when gauge is not NULL,
 * the measure function of the tests with gauge.
 */
static argx_help_options_t *create_options(size_t width, gauge_t *gauge)
{
    argx_help_options_t *options = NULL;

    NAOS_REQUIRE_TRUE(argx_help_options_create(&options) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(options != NULL);
    argx_help_options_set_width(options, width);
    if (gauge != NULL)
    {
        argx_help_options_set_measure(options, gauge_measure, gauge);
    }
    return options;
}

/* Adds a flag with a description; it must succeed. */
static void add_flag(argx_command_t *command, char short_name, const char *long_name,
                     const char *description)
{
    argx_option_t *option = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_flag(command, short_name, long_name, &option) ==
                      ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_description(option, description) == ARGX_ERROR_NONE);
}

/*
 * Adds an option with a value of strings, with the value name, default
 * value (or NULL) and description given; it must succeed.
 */
static void add_value(argx_command_t *command, const char *long_name, const char *value_name,
                      const char *default_value, const char *description)
{
    argx_option_t *option = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_value(command, '\0', long_name, argx_type_string(),
                                             &option) == ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_option_set_value_name(option, value_name) == ARGX_ERROR_NONE);
    if (default_value != NULL)
    {
        NAOS_REQUIRE_TRUE(argx_option_set_default(option, default_value) == ARGX_ERROR_NONE);
    }
    NAOS_REQUIRE_TRUE(argx_option_set_description(option, description) == ARGX_ERROR_NONE);
}

/* Adds a positional of strings with a description; it must succeed. */
static void add_positional(argx_command_t *command, const char *name, const char *description)
{
    argx_positional_t *positional = NULL;

    NAOS_REQUIRE_TRUE(argx_command_add_positional(command, name, argx_type_string(),
                                                  ARGX_ARITY_REQUIRED, &positional) ==
                      ARGX_ERROR_NONE);
    NAOS_REQUIRE_TRUE(argx_positional_set_description(positional, description) ==
                      ARGX_ERROR_NONE);
}

/*
 * Has the help of command written with options, and checks that the call
 * succeeds, that the text is exactly expected, and that argx_help_format()
 * gives the same text.
 */
static void check_help(const argx_command_t *command, const argx_help_options_t *options,
                       const char *expected)
{
    sink_t sink;
    char buffer[SINK_SIZE];
    size_t length = 0;

    memset(&sink, 0, sizeof sink);
    NAOS_CHECK_TRUE(argx_help_write(command, options, sink_write, &sink) == ARGX_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(sink.text, sink.length, expected, strlen(expected));

    NAOS_CHECK_TRUE(argx_help_format(command, options, buffer, sizeof buffer, &length) ==
                    ARGX_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(buffer, length, expected, strlen(expected));
}

/* Like check_help(), for the usage line alone. */
static void check_usage(const argx_command_t *command, const argx_help_options_t *options,
                        const char *expected)
{
    sink_t sink;
    char buffer[SINK_SIZE];
    size_t length = 0;

    memset(&sink, 0, sizeof sink);
    NAOS_CHECK_TRUE(argx_help_write_usage(command, options, sink_write, &sink) ==
                    ARGX_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(sink.text, sink.length, expected, strlen(expected));

    NAOS_CHECK_TRUE(argx_help_format_usage(command, options, buffer, sizeof buffer, &length) ==
                    ARGX_ERROR_NONE);
    NAOS_CHECK_MEM_EQ(buffer, length, expected, strlen(expected));
}

/* "出力先": 9 bytes, 6 columns. */
#define OUTPUT_NAME "\xE5\x87\xBA\xE5\x8A\x9B\xE5\x85\x88"

/* "入力ファイル" and "出力ファイル": 18 bytes, 12 columns each. */
#define INPUT_FILE "\xE5\x85\xA5\xE5\x8A\x9B\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB"
#define OUTPUT_FILE "\xE5\x87\xBA\xE5\x8A\x9B\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB"

/* "あいう", "えおか", "きくけ" and "こさし": 9 bytes, 6 columns each. */
#define KANA_1 "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86"
#define KANA_2 "\xE3\x81\x88\xE3\x81\x8A\xE3\x81\x8B"
#define KANA_3 "\xE3\x81\x8D\xE3\x81\x8F\xE3\x81\x91"
#define KANA_4 "\xE3\x81\x93\xE3\x81\x95\xE3\x81\x97"

/* ---- The description column ------------------------------------------------ */

/*
 * The description column follows the measured width of the widest
 * heading: "    --out <出力先>" is 18 columns, so the column is 2 + 18 + 2
 * = 22. Counted in bytes, the heading is 21 and the column 25.
 */
NAOS_TEST(description_column_follows_the_measured_heading)
{
    static const char measured[] =
        "Usage: tool [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -v, --verbose       Say more\n"
        "      --out <" OUTPUT_NAME ">  Write here\n";
    static const char bytes[] =
        "Usage: tool [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -v, --verbose          Say more\n"
        "      --out <" OUTPUT_NAME ">  Write here\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *with = NULL;
    argx_help_options_t *without = NULL;

    add_flag(root, 'v', "verbose", "Say more");
    add_value(root, "out", OUTPUT_NAME, NULL, "Write here");
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    with = create_options(80, &gauge);
    without = create_options(80, NULL);

    check_help(root, with, measured);
    check_help(root, without, bytes);
    check_help(root, NULL, bytes);
    NAOS_CHECK_TRUE(gauge.calls != 0);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(with);
    argx_help_options_destroy(without);
    destroy_root(root);
}

/*
 * The description column follows the measured width of a positional's
 * heading as well, and a heading too wide for the limit still sends its
 * description to the next line.
 */
NAOS_TEST(description_column_follows_a_measured_argument)
{
    static const char measured[] =
        "Usage: tool <" INPUT_FILE ">\n"
        "\n"
        "Arguments:\n"
        "  <" INPUT_FILE ">  Read this\n";
    static const char narrow[] =
        "Usage: tool\n"
        "       <" INPUT_FILE ">\n"
        "\n"
        "Arguments:\n"
        "  <" INPUT_FILE ">\n"
        "          Read this\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;

    add_positional(root, INPUT_FILE, "Read this");
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    options = create_options(80, &gauge);
    check_help(root, options, measured);

    /* Width 20: the limit is 10, less than 2 + 14 + 2. */
    argx_help_options_set_width(options, 20);
    check_help(root, options, narrow);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(options);
    destroy_root(root);
}

/* ---- Breaking into lines --------------------------------------------------- */

/*
 * A description is broken by the measured width of its words: at width
 * 20, "あいう えおか きくけ" is 20 columns and fits; in bytes it is 29.
 */
NAOS_TEST(description_breaks_by_the_measured_width)
{
    static const char measured[] =
        "Usage: tool\n"
        "\n" KANA_1 " " KANA_2 " " KANA_3 "\n" KANA_4 "\n";
    static const char bytes[] =
        "Usage: tool\n"
        "\n" KANA_1 " " KANA_2 "\n" KANA_3 " " KANA_4 "\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *with = NULL;
    argx_help_options_t *without = NULL;

    NAOS_REQUIRE_TRUE(argx_command_set_description(root, KANA_1 " " KANA_2 " " KANA_3
                                                         " " KANA_4) == ARGX_ERROR_NONE);
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    with = create_options(20, &gauge);
    without = create_options(20, NULL);

    check_help(root, with, measured);
    check_help(root, without, bytes);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(with);
    argx_help_options_destroy(without);
    destroy_root(root);
}

/*
 * The annotations are measured with the description: the default value
 * "あいう" is 6 columns, so "[default: あいう]" is 2 words of 9 and 7
 * columns.
 */
NAOS_TEST(annotations_are_measured)
{
    static const char measured[] =
        "Usage: tool [OPTIONS]\n"
        "\n"
        "Options:\n"
        "      --name <N>  Set it [default:\n"
        "                  " KANA_1 "]\n";
    static const char wide[] =
        "Usage: tool [OPTIONS]\n"
        "\n"
        "Options:\n"
        "      --name <N>  Set it [default: " KANA_1 "]\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;

    add_value(root, "name", "N", KANA_1, "Set it");
    validate(root);

    /*
     * The description starts at 18. "Set it [default:" ends at 34; with
     * " あいう]" it would end at 42.
     */
    gauge_init(&gauge, GAUGE_UTF8);
    options = create_options(41, &gauge);
    check_help(root, options, measured);

    argx_help_options_set_width(options, 42);
    check_help(root, options, wide);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(options);
    destroy_root(root);
}

/*
 * A description without blanks that its author broke with newlines keeps
 * those lines when each fits the measured width; counted in bytes, the
 * same lines go beyond it, but a word is never split either way.
 */
NAOS_TEST(lines_broken_by_the_author_stay)
{
    static const char expected[] =
        "Usage: tool [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -v, --verbose  " KANA_1 KANA_2 "\n"
        "                 " KANA_3 KANA_4 "\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;

    add_flag(root, 'v', "verbose", KANA_1 KANA_2 "\n" KANA_3 KANA_4);
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    /* Width 34: the column is 17, and each line ends at 29. */
    options = create_options(34, &gauge);
    check_help(root, options, expected);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(options);
    destroy_root(root);
}

/*
 * The usage line breaks by the measured width of its items: at width 30,
 * "Usage: tool <入力ファイル>" is 26 columns and fits; in bytes the item
 * alone is 20.
 */
NAOS_TEST(usage_line_breaks_by_the_measured_width)
{
    static const char measured[] =
        "Usage: tool <" INPUT_FILE ">\n"
        "       <" OUTPUT_FILE ">\n";
    static const char bytes[] =
        "Usage: tool\n"
        "       <" INPUT_FILE ">\n"
        "       <" OUTPUT_FILE ">\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *with = NULL;
    argx_help_options_t *without = NULL;

    add_positional(root, INPUT_FILE, "Read this");
    add_positional(root, OUTPUT_FILE, "Write this");
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    with = create_options(30, &gauge);
    without = create_options(30, NULL);

    check_usage(root, with, measured);
    check_usage(root, without, bytes);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(with);
    argx_help_options_destroy(without);
    destroy_root(root);
}

/* ---- The setting ----------------------------------------------------------- */

/*
 * The measure function receives the user data given, and NULL restores
 * one byte per column: the text is then the one of the default options.
 */
NAOS_TEST(null_restores_the_default)
{
    static const char bytes[] =
        "Usage: tool [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -v, --verbose          Say more\n"
        "      --out <" OUTPUT_NAME ">  Write here\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;

    add_flag(root, 'v', "verbose", "Say more");
    add_value(root, "out", OUTPUT_NAME, NULL, "Write here");
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    options = create_options(80, &gauge);
    argx_help_options_set_measure(options, NULL, &gauge);
    check_help(root, options, bytes);
    NAOS_CHECK_UINT_EQ(gauge.calls, 0);

    argx_help_options_destroy(options);
    destroy_root(root);
}

/*
 * Without a measure function nothing changes for ASCII text, and an
 * ASCII-only help is the same with the measure function of the tests,
 * which counts ASCII as 1 column per byte.
 */
NAOS_TEST(ascii_help_is_the_same_with_a_measure_function)
{
    static const char expected[] =
        "Usage: tool [OPTIONS] <file>\n"
        "\n"
        "Copies a file to the place given, or to the current directory when no place is\n"
        "given.\n"
        "\n"
        "Arguments:\n"
        "  <file>          The file to copy\n"
        "\n"
        "Options:\n"
        "  -v, --verbose   Say more\n"
        "      --to <DIR>  Where the copy goes [default: .]\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;

    NAOS_REQUIRE_TRUE(argx_command_set_description(
                          root, "Copies a file to the place given, or to the current directory "
                                "when no place is given.") == ARGX_ERROR_NONE);
    add_positional(root, "file", "The file to copy");
    add_flag(root, 'v', "verbose", "Say more");
    add_value(root, "to", "DIR", ".", "Where the copy goes");
    validate(root);

    gauge_init(&gauge, GAUGE_UTF8);
    options = create_options(80, &gauge);
    check_help(root, NULL, expected);
    check_help(root, options, expected);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(options);
    destroy_root(root);
}

/* ---- Unusual measure functions ---------------------------------------------- */

/*
 * Huge widths saturate instead of wrapping around: every usage line item
 * goes to a line of its own, and every description to the line after its
 * heading, at the limit of the description column.
 */
NAOS_TEST(huge_widths_saturate)
{
    static const char expected[] =
        "Usage: tool\n"
        "       [OPTIONS]\n"
        "       <file>\n"
        "\n"
        "Copies\n"
        "a\n"
        "file\n"
        "\n"
        "Arguments:\n"
        "  <file>\n"
        "                                The\n"
        "                                file\n"
        "\n"
        "Options:\n"
        "  -v, --verbose\n"
        "                                Say\n"
        "                                more\n";
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;

    NAOS_REQUIRE_TRUE(argx_command_set_description(root, "Copies a file") == ARGX_ERROR_NONE);
    add_positional(root, "file", "The file");
    add_flag(root, 'v', "verbose", "Say more");
    validate(root);

    gauge_init(&gauge, GAUGE_HUGE);
    options = create_options(80, &gauge);
    check_help(root, options, expected);
    NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);

    argx_help_options_destroy(options);
    destroy_root(root);
}

/*
 * A measure function that gives the same text a different width at each
 * call breaks the layout, but the generation still succeeds, writes every
 * byte of the schema's strings and keeps the shape of a help text.
 */
NAOS_TEST(changing_widths_keep_the_generation_defined)
{
    argx_command_t *root = create_root();
    gauge_t gauge;
    argx_help_options_t *options = NULL;
    sink_t sink;
    size_t width;

    NAOS_REQUIRE_TRUE(argx_command_set_description(root, "Copies a file to the place given") ==
                      ARGX_ERROR_NONE);
    add_positional(root, "file", "The file to copy");
    add_flag(root, 'v', "verbose", "Say more about what happens");
    add_value(root, "to", "DIR", ".", "Where the copy goes");
    validate(root);

    for (width = 0; width <= 60; width += 5)
    {
        gauge_init(&gauge, GAUGE_CHANGING);
        options = create_options(width, &gauge);
        memset(&sink, 0, sizeof sink);
        NAOS_CHECK_TRUE(argx_help_write(root, options, sink_write, &sink) == ARGX_ERROR_NONE);
        NAOS_REQUIRE_TRUE(sink.length >= 2);
        NAOS_CHECK_TRUE(sink.text[sink.length - 1] == '\n');
        NAOS_CHECK_UINT_EQ(gauge.bad_calls, 0);
        argx_help_options_destroy(options);
    }

    destroy_root(root);
}

/* ---- The registration ------------------------------------------------------ */

NAOS_SUITE(help_measure,
    NAOS_ADD_TEST(description_column_follows_the_measured_heading),
    NAOS_ADD_TEST(description_column_follows_a_measured_argument),
    NAOS_ADD_TEST(description_breaks_by_the_measured_width),
    NAOS_ADD_TEST(annotations_are_measured),
    NAOS_ADD_TEST(lines_broken_by_the_author_stay),
    NAOS_ADD_TEST(usage_line_breaks_by_the_measured_width),
    NAOS_ADD_TEST(null_restores_the_default),
    NAOS_ADD_TEST(ascii_help_is_the_same_with_a_measure_function),
    NAOS_ADD_TEST(huge_widths_saturate),
    NAOS_ADD_TEST(changing_widths_keep_the_generation_defined)
)

NAOS_MAIN(
    NAOS_ADD_SUITE(help_measure)
)
