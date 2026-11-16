/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * ArgX - internal declarations for the help output: the layer that counts
 * columns while it passes text to a write function.
 *
 * Everything the help writes goes through an argx_out_t. It tracks the
 * column of the current line, and the total width of everything written
 * since it was started, across lines.
 *
 * Text is measured by a measure function (argx_help_measure_fn_t in
 * <argx/help.h>), when one is set with argx_out_set_measure(). Without
 * one, one byte is one column, whatever the byte. A blank that is
 * requested with argx_out_spaces() is one column and is never measured,
 * and neither is a newline. This is the only place that calls the measure
 * function.
 *
 * Blanks are not written when they are requested. The output remembers
 * how many there are and writes them when text follows on the same line;
 * a newline drops them. So no line ends with a blank, whatever the
 * callers do. Remembered blanks count toward the column at once.
 *
 * Once the write function has reported a failure, no function calls it
 * again. Columns are still counted, so callers need not check for the
 * failure after every call.
 *
 * Without a write function (NULL), nothing is written and nothing fails;
 * only the columns are counted. This measures the width of a text with
 * the same code that writes it: see argx_out_total().
 *
 * Columns and the total stop at the greatest size_t value instead of
 * wrapping around, whatever the measure function returns.
 *
 * The functions allocate nothing and know nothing about a schema. An
 * argx_out_t belongs to the caller and is used for one generation by one
 * thread.
 *
 * Preconditions of every function: out is not NULL (checked with assert),
 * and, except for argx_out_init(), it was initialised with
 * argx_out_init() (not checked).
 */
#ifndef ARGX_OUT_H
#define ARGX_OUT_H

#include <argx/help.h>

#include <stdbool.h>
#include <stddef.h>

/*
 * The output state. Only the functions in this header read and write its
 * members.
 */
typedef struct argx_out
{
    argx_help_write_fn_t write_fn;     /* NULL: only the columns are counted */
    void *user_data;                   /* passed to write_fn unchanged */
    argx_help_measure_fn_t measure_fn; /* NULL: one byte is one column */
    void *measure_data;                /* passed to measure_fn unchanged */
    size_t column;                     /* the current column, including unwritten blanks */
    size_t total;                      /* the width of everything since argx_out_init() */
    size_t pending;                    /* the number of unwritten blanks */
    bool failed;                       /* write_fn has reported a failure */
} argx_out_t;

/*
 * Starts an output at column 0 and total 0, with no remembered blanks, no
 * failure and no measure function. write_fn receives the text along with
 * user_data. When write_fn is NULL, nothing is written and only the
 * columns are counted.
 */
void argx_out_init(argx_out_t *out, argx_help_write_fn_t write_fn, void *user_data);

/*
 * Sets the function that measures text, and the pointer passed to it.
 * NULL restores the default: one byte is one column.
 */
void argx_out_set_measure(argx_out_t *out, argx_help_measure_fn_t measure_fn,
                          void *measure_data);

/*
 * Returns the width of length bytes of text, in columns: what the measure
 * function returns for them or, without one, length. Returns 0 for 0
 * bytes without calling the function. Writes nothing.
 *
 * Preconditions: text is not NULL unless length is 0, and it holds no
 * '\n'. They are checked with assert.
 */
size_t argx_out_measure(const argx_out_t *out, const char *text, size_t length);

/*
 * Writes length bytes of text: first the remembered blanks, then the text.
 * The column and the total advance by the text's width (see
 * argx_out_measure()). When length is 0, nothing happens and the blanks
 * stay remembered.
 *
 * Preconditions: text is not NULL unless length is 0, and it holds no
 * '\n' (argx_out_newline() ends a line). They are checked with assert.
 */
void argx_out_text(argx_out_t *out, const char *text, size_t length);

/*
 * Like argx_out_text(), but advances by width, which the caller has
 * already measured with argx_out_measure() for the same text. The text is
 * not measured again.
 *
 * Preconditions: as for argx_out_text().
 */
void argx_out_text_measured(argx_out_t *out, const char *text, size_t length, size_t width);

/*
 * Like argx_out_text(), but takes the length from the string text.
 *
 * Preconditions: text is not NULL and holds no '\n'. They are checked with
 * assert.
 */
void argx_out_string(argx_out_t *out, const char *text);

/*
 * Remembers count more blanks and advances the column and the total by
 * count. Nothing is written yet.
 */
void argx_out_spaces(argx_out_t *out, size_t count);

/*
 * When the current column is left of column, remembers the blanks needed
 * to reach it, as argx_out_spaces() does. Otherwise nothing happens.
 */
void argx_out_pad_to(argx_out_t *out, size_t column);

/*
 * Ends the line: drops the remembered blanks, writes a single '\n' byte
 * and sets the column to 0. The total advances by 1 for the newline; the
 * dropped blanks stay counted in it.
 */
void argx_out_newline(argx_out_t *out);

/*
 * Returns the current column: the width of the text written on this line
 * plus the number of remembered blanks.
 */
size_t argx_out_column(const argx_out_t *out);

/*
 * Returns the total: the width of all the text and blanks since
 * argx_out_init(), plus 1 for each newline. It does not restart at a
 * newline, so it measures the width of a text that has a newline inside,
 * such as a heading with a newline inside a name.
 */
size_t argx_out_total(const argx_out_t *out);

/*
 * Returns true when the write function has reported a failure. Always
 * false without a write function.
 */
bool argx_out_failed(const argx_out_t *out);

#endif /* ARGX_OUT_H */
