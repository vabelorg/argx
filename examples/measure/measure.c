/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * ArgX example - a help in Japanese, laid out by display width.
 *
 * The help options get a measure function built on mbrtowc() and
 * wcwidth() (POSIX), so a kanji or a kana counts as 2 columns, as a
 * terminal shows it. The descriptions are broken by their author with
 * "\n", because Japanese has no blanks to break at; each line fits the
 * width, so the library leaves it as it is. See the How-to: "Measuring
 * the display width of the help" and "Writing help text in Chinese or
 * Japanese".
 *
 * The measure function needs a UTF-8 locale: main() calls
 * setlocale(LC_CTYPE, ""). In another locale, every byte that mbrtowc()
 * cannot read counts as 1 column, as the library counts without a
 * measure function.
 *
 *     $ LANG=ja_JP.UTF-8 ./measure --help
 *     Usage: measure [OPTIONS] <入力>
 *
 *     画像を変換します。
 *
 *     Arguments:
 *       <入力>               変換する画像のファイル
 *
 *     Options:
 *       -h, --help           このヘルプを表示します。
 *       -o, --output <出力>  変換した画像を書き込むファイル。
 *                            省略すると、入力と同じ場所に書き込みます。
 */
#define _XOPEN_SOURCE 700

#include <argx/argx.h>

#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Checks a call that can fail; on a failure, goes to the message. */
#define TRY(call)                     \
    do                                \
    {                                 \
        error = (call);               \
        if (error != ARGX_ERROR_NONE) \
        {                             \
            goto define_failed;       \
        }                             \
    } while (0)

/*
 * Measures with mbrtowc() and wcwidth(): the width of length bytes of
 * text, in columns. A byte that does not start a valid character, and a
 * character that wcwidth() cannot measure, count as 1 column.
 */
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
                n = 1; /* a '\0' byte */
            }
            w = wcwidth(wc);
        }
        total += (w < 0) ? 1 : (size_t)w;
        text += n;
        length -= n;
    }
    return total;
}

/* The library passes the help text to this function, part by part. */
static bool write_to_stream(void *user_data, const char *text, size_t length)
{
    return fwrite(text, 1, length, (FILE *)user_data) == length;
}

int main(int argc, char **argv)
{
    argx_command_t *root = NULL;
    argx_option_t *help = NULL;
    argx_option_t *output = NULL;
    argx_positional_t *input = NULL;
    argx_help_options_t *options = NULL;
    argx_result_t *result = NULL;
    argx_error_t error;
    int status = EXIT_FAILURE;

    /* wcwidth() reads the character set of the locale. */
    setlocale(LC_CTYPE, "");

    TRY(argx_command_create("measure", &root));
    TRY(argx_command_set_description(root, "画像を変換します。"));

    TRY(argx_command_add_flag(root, 'h', "help", &help));
    TRY(argx_option_set_description(help, "このヘルプを表示します。"));
    TRY(argx_option_set_terminal(help, true));

    TRY(argx_command_add_value(root, 'o', "output", argx_type_string(), &output));
    TRY(argx_option_set_value_name(output, "出力"));
    /* Japanese has no blanks: the author breaks the line. */
    TRY(argx_option_set_description(output, "変換した画像を書き込むファイル。\n"
                                             "省略すると、入力と同じ場所に書き込みます。"));

    TRY(argx_command_add_positional(root, "入力", argx_type_string(), ARGX_ARITY_REQUIRED,
                                    &input));
    TRY(argx_positional_set_description(input, "変換する画像のファイル"));

    TRY(argx_command_validate(root, NULL, NULL, NULL));

    TRY(argx_help_options_create(&options));
    argx_help_options_set_measure(options, measure_with_wcwidth, NULL);

    error = argx_parse(root, argc, argv, &result);
    switch (error)
    {
    case ARGX_ERROR_NONE:
        if (argx_result_flag(result, help))
        {
            if (argx_help_write(root, options, write_to_stream, stdout) == ARGX_ERROR_NONE)
            {
                status = EXIT_SUCCESS;
            }
        }
        else
        {
            const char *path = NULL;

            if (argx_result_positional_string(result, input, 0, &path))
            {
                printf("%s\n", path);
                status = EXIT_SUCCESS;
            }
        }
        break;

    case ARGX_ERROR_INVALID_COMMAND_LINE:
        fprintf(stderr, "measure: %s\n", argx_result_error_message(result));
        /* The usage line is measured with the same help options. */
        argx_help_write_usage(argx_result_error_command(result), options, write_to_stream,
                              stderr);
        break;

    default:
        fprintf(stderr, "measure: %s\n", argx_error_message(error));
        break;
    }

    goto cleanup;

define_failed:
    fprintf(stderr, "measure: cannot define the command line: %s\n", argx_error_message(error));

cleanup:
    argx_result_destroy(result);
    argx_help_options_destroy(options);
    argx_command_destroy(root);

    return status;
}
