/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * ArgX - the help output: counts columns while it passes text to a write
 * function.
 */
#include "out.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * The blanks passed to the write function in one call. More blanks than
 * this take several calls.
 */
#define ARGX_OUT_BLANKS "                                "
#define ARGX_OUT_BLANKS_LENGTH (sizeof ARGX_OUT_BLANKS - 1)

/*
 * Returns a + b, or the greatest size_t value when the sum would exceed
 * it.
 */
static size_t out_add(size_t a, size_t b)
{
    return (b > ((size_t)-1 - a)) ? (size_t)-1 : (a + b);
}

/*
 * Advances the column and the total by width.
 */
static void out_advance(argx_out_t *out, size_t width)
{
    out->column = out_add(out->column, width);
    out->total = out_add(out->total, width);
}

/*
 * Passes length bytes of text to the write function, unless there is none
 * or it has already failed, and records a failure. This is the only place
 * that calls the write function. length is not 0.
 */
static void out_write(argx_out_t *out, const char *text, size_t length)
{
    assert(length != 0);

    if ((out->write_fn == NULL) || out->failed)
    {
        return;
    }

    if (!out->write_fn(out->user_data, text, length))
    {
        out->failed = true;
    }
}

/*
 * Writes the remembered blanks and forgets them. The column already
 * counts them.
 */
static void out_flush_blanks(argx_out_t *out)
{
    size_t rest = out->pending;

    out->pending = 0;
    while (rest != 0)
    {
        size_t part = (rest < ARGX_OUT_BLANKS_LENGTH) ? rest : ARGX_OUT_BLANKS_LENGTH;

        out_write(out, ARGX_OUT_BLANKS, part);
        rest -= part;
    }
}

void argx_out_init(argx_out_t *out, argx_help_write_fn_t write_fn, void *user_data)
{
    assert(out != NULL);

    out->write_fn = write_fn;
    out->user_data = user_data;
    out->measure_fn = NULL;
    out->measure_data = NULL;
    out->column = 0;
    out->total = 0;
    out->pending = 0;
    out->failed = false;
}

void argx_out_set_measure(argx_out_t *out, argx_help_measure_fn_t measure_fn,
                          void *measure_data)
{
    assert(out != NULL);

    out->measure_fn = measure_fn;
    out->measure_data = measure_data;
}

size_t argx_out_measure(const argx_out_t *out, const char *text, size_t length)
{
    assert(out != NULL);

    if (length == 0)
    {
        return 0;
    }

    assert(text != NULL);
    assert(memchr(text, '\n', length) == NULL);

    if (out->measure_fn == NULL)
    {
        return length;
    }

    return out->measure_fn(out->measure_data, text, length);
}

void argx_out_text(argx_out_t *out, const char *text, size_t length)
{
    argx_out_text_measured(out, text, length, argx_out_measure(out, text, length));
}

void argx_out_text_measured(argx_out_t *out, const char *text, size_t length, size_t width)
{
    assert(out != NULL);

    if (length == 0)
    {
        return;
    }

    assert(text != NULL);
    assert(memchr(text, '\n', length) == NULL);

    out_flush_blanks(out);
    out_write(out, text, length);
    out_advance(out, width);
}

void argx_out_string(argx_out_t *out, const char *text)
{
    assert(text != NULL);

    argx_out_text(out, text, strlen(text));
}

void argx_out_spaces(argx_out_t *out, size_t count)
{
    assert(out != NULL);

    out->pending = out_add(out->pending, count);
    out_advance(out, count);
}

void argx_out_pad_to(argx_out_t *out, size_t column)
{
    assert(out != NULL);

    if (out->column < column)
    {
        argx_out_spaces(out, column - out->column);
    }
}

void argx_out_newline(argx_out_t *out)
{
    assert(out != NULL);

    /* The blanks at the end of the line are dropped, not written. */
    out->pending = 0;
    out_write(out, "\n", 1);
    out->column = 0;
    out->total = out_add(out->total, 1);
}

size_t argx_out_column(const argx_out_t *out)
{
    assert(out != NULL);

    return out->column;
}

size_t argx_out_total(const argx_out_t *out)
{
    assert(out != NULL);

    return out->total;
}

bool argx_out_failed(const argx_out_t *out)
{
    assert(out != NULL);

    return out->failed;
}
