/*
 * Copyright (c) 2026 The ArgX Authors
 * SPDX-License-Identifier: MIT
 *
 * ArgX - the parse result: its creation and destruction, the copy of the
 * arguments, the selected commands, the values, the parse error, and the
 * functions that read a result.
 *
 * This is the only source that reads and writes a result's fields.
 */
#include "parse_internal.h"

#include "alloc_internal.h"
#include "message.h"
#include "utf8.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Returns the frame for the selected command at index. */
static argx_result_frame_t *frame_at(const argx_result_t *result, size_t index)
{
    return (argx_result_frame_t *)argx_vec_at(&result->frames, index);
}

/* ---- The copy of the arguments ------------------------------------------ */

/*
 * Copies the count strings of argv into one allocation, with the pointer
 * array first and the strings after it, and writes it to *out. The bytes
 * are copied unchanged. With a count of 0, nothing is allocated and *out
 * is NULL.
 *
 * Returns ARGX_ERROR_OUT_OF_MEMORY when the allocation fails or the size
 * of the copy does not fit in size_t; *out is then NULL.
 */
static argx_error_t copy_args(size_t count, char *const *argv, char ***out)
{
    size_t size;
    char **args;
    char *next;
    size_t i;

    *out = NULL;
    if (count == 0)
    {
        return ARGX_ERROR_NONE;
    }

    if (count > (SIZE_MAX / sizeof(char *)))
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    size = count * sizeof(char *);
    for (i = 0; i < count; ++i)
    {
        size_t length;

        assert(argv[i] != NULL);

        length = strlen(argv[i]);
        if (length > (SIZE_MAX - size))
        {
            return ARGX_ERROR_OUT_OF_MEMORY;
        }

        size += length;
        if (size == SIZE_MAX)
        {
            return ARGX_ERROR_OUT_OF_MEMORY;
        }

        size += 1;
    }

    args = (char **)argx_malloc(size);
    if (args == NULL)
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    next = (char *)args + (count * sizeof(char *));
    for (i = 0; i < count; ++i)
    {
        size_t length = strlen(argv[i]) + 1;

        memcpy(next, argv[i], length);
        args[i] = next;
        next += length;
    }

    *out = args;

    return ARGX_ERROR_NONE;
}

/*
 * The same for the count wide strings of argv, which are converted to
 * UTF-8 ("utf8.h"): each unit that cannot be converted becomes U+FFFD.
 * *first_invalid is set to the index of the first string after argv[0]
 * that has such a unit, or to 0 when there is none; a unit in argv[0] that
 * cannot be converted is replaced but not reported.
 *
 * The UTF-8 length of a single string is not checked against size_t.
 * Where a unit is four bytes wide, it gives at most four bytes, so the
 * count is no more than the size of the wide string, an object that
 * exists. Where a unit is two bytes wide, it gives at most three bytes,
 * which is more than its own size. There the count stays in range because
 * an object has at most PTRDIFF_MAX bytes, so a string has at most
 * SIZE_MAX / 4 units, and three times that is less than SIZE_MAX. The sum
 * over the strings is checked as in copy_args().
 */
static argx_error_t copy_args_wide(size_t count, wchar_t *const *argv, char ***out, size_t *first_invalid)
{
    size_t size;
    char **args;
    char *next;
    size_t i;

    *out = NULL;
    *first_invalid = 0;
    if (count == 0)
    {
        return ARGX_ERROR_NONE;
    }

    if (count > (SIZE_MAX / sizeof(char *)))
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    size = count * sizeof(char *);
    for (i = 0; i < count; ++i)
    {
        size_t length;
        bool valid;

        assert(argv[i] != NULL);

        length = argx_utf8_length(argv[i], &valid);
        if (!valid && (i != 0) && (*first_invalid == 0))
        {
            *first_invalid = i;
        }

        if (length > (SIZE_MAX - size))
        {
            return ARGX_ERROR_OUT_OF_MEMORY;
        }

        size += length;
        if (size == SIZE_MAX)
        {
            return ARGX_ERROR_OUT_OF_MEMORY;
        }

        size += 1;
    }

    args = (char **)argx_malloc(size);
    if (args == NULL)
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    next = (char *)args + (count * sizeof(char *));
    for (i = 0; i < count; ++i)
    {
        args[i] = next;
        next += argx_utf8_write(argv[i], next) + 1;
    }

    *out = args;

    return ARGX_ERROR_NONE;
}

/* ---- Creation and destruction ------------------------------------------- */

/*
 * Allocates a result for root with no arguments, no selected command and
 * no parse error, and writes it to *out. Returns ARGX_ERROR_OUT_OF_MEMORY
 * when the allocation fails; *out is then NULL.
 */
static argx_error_t new_result(const argx_command_t *root, argx_result_t **out)
{
    argx_result_t *result = (argx_result_t *)argx_malloc(sizeof(*result));

    *out = result;
    if (result == NULL)
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    result->root = root;
    result->args = NULL;
    result->arg_count = 0;
    result->first_invalid_arg = 0;
    argx_vec_init(&result->frames, sizeof(argx_result_frame_t));
    argx_vec_init(&result->texts, sizeof(char *));
    result->error = ARGX_ERROR_NONE;
    result->error_kind = ARGX_PARSE_ERROR_NONE;
    result->error_source = ARGX_SOURCE_NONE;
    result->error_index = 0;
    result->error_argument = NULL;
    result->error_value = NULL;
    result->error_command = NULL;
    result->error_option = NULL;
    result->error_positional = NULL;
    result->error_short_name = '\0';
    result->error_message = NULL;
    result->terminal = NULL;

    return ARGX_ERROR_NONE;
}

/*
 * Finishes creating result, whose copy of count arguments ended with the
 * outcome error: selects root and writes result to *out. When the copy
 * failed, or root cannot be selected, result is destroyed as a whole and
 * *out stays NULL. The two functions that create a result differ only in
 * how they copy the arguments.
 */
static argx_error_t finish_result(argx_result_t *result, argx_error_t error, size_t count,
                                  argx_result_t **out)
{
    if (error == ARGX_ERROR_NONE)
    {
        result->arg_count = count;
        error = argx_result_push_command(result, result->root);
    }

    if (error != ARGX_ERROR_NONE)
    {
        argx_result_destroy(result);
        return error;
    }

    *out = result;

    return ARGX_ERROR_NONE;
}

argx_error_t argx_result_create(const argx_command_t *root, int argc, char *const *argv,
                                argx_result_t **out)
{
    argx_result_t *result;
    argx_error_t error;

    assert(root != NULL);
    assert(out != NULL);
    assert(argx_command_parent(root) == NULL);
    assert(argc >= 0);
    assert((argc == 0) || (argv != NULL));

    *out = NULL;

    error = new_result(root, &result);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    error = copy_args((size_t)argc, argv, &result->args);

    return finish_result(result, error, (size_t)argc, out);
}

argx_error_t argx_result_create_wide(const argx_command_t *root, int argc, wchar_t *const *argv,
                                     argx_result_t **out)
{
    argx_result_t *result;
    argx_error_t error;

    assert(root != NULL);
    assert(out != NULL);
    assert(argx_command_parent(root) == NULL);
    assert(argc >= 0);
    assert((argc == 0) || (argv != NULL));

    *out = NULL;

    error = new_result(root, &result);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    error = copy_args_wide((size_t)argc, argv, &result->args, &result->first_invalid_arg);

    return finish_result(result, error, (size_t)argc, out);
}

/*
 * Cleans up the value of type in the area value, then frees the area.
 * This is the only place that releases a value.
 */
static void destroy_value(const argx_type_t *type, void *value)
{
    argx_type_release(type, value);
    argx_free(value);
}

/*
 * Cleans up the value of every item of items, an array of
 * argx_result_item_t holding values of type, and makes the array empty
 * again, as after argx_vec_init().
 */
static void destroy_items(const argx_type_t *type, argx_vec_t *items)
{
    size_t k;

    for (k = 0; k < items->count; ++k)
    {
        destroy_value(type, ((argx_result_item_t *)argx_vec_at(items, k))->value);
    }

    argx_vec_destroy(items);
}

/*
 * Cleans up every value held in frame's slots, once each, and frees the
 * slots.
 */
static void destroy_frame(argx_result_frame_t *frame)
{
    size_t i;

    for (i = 0; i < frame->option_count; ++i)
    {
        const argx_type_t *type = argx_option_type(argx_command_option_at(frame->command, i));

        if (frame->options[i].value != NULL)
        {
            destroy_value(type, frame->options[i].value);
        }

        /* Only a list option has items, and only it needs type then. */
        if (frame->options[i].items.count > 0)
        {
            destroy_items(type, &frame->options[i].items);
        }
    }

    for (i = 0; i < frame->positional_count; ++i)
    {
        destroy_items(argx_positional_type(argx_command_positional_at(frame->command, i)),
                      &frame->positionals[i].values);
    }

    argx_free(frame->positionals);
    argx_free(frame->options);
}

void argx_result_destroy(argx_result_t *result)
{
    size_t i;

    if (result == NULL)
    {
        return;
    }

    for (i = 0; i < result->frames.count; ++i)
    {
        destroy_frame(frame_at(result, i));
    }

    argx_vec_destroy(&result->frames);
    for (i = 0; i < result->texts.count; ++i)
    {
        argx_free(*(char **)argx_vec_at(&result->texts, i));
    }
    argx_vec_destroy(&result->texts);
    argx_free(result->error_message);
    argx_free(result->args);
    argx_free(result);
}

/* ---- The arguments ------------------------------------------------------ */

const char *argx_result_arg(const argx_result_t *result, size_t index)
{
    assert(result != NULL);
    assert(index < result->arg_count);

    return result->args[index];
}

size_t argx_result_first_invalid_arg(const argx_result_t *result)
{
    assert(result != NULL);

    return result->first_invalid_arg;
}

/* ---- The selected commands ---------------------------------------------- */

/* Sets slot to the "not given" state of option. */
static void init_option_slot(argx_option_slot_t *slot, const argx_option_t *option)
{
    slot->source = ARGX_SOURCE_NONE;
    slot->on = false;
    slot->count = 0;
    slot->value = NULL;
    slot->index = 0;
    argx_vec_init(&slot->items, sizeof(argx_result_item_t));

    switch (argx_option_kind(option))
    {
    case ARGX_OPTION_FLAG:
    case ARGX_OPTION_COUNT:
        slot->source = ARGX_SOURCE_DEFAULT;
        break;
    case ARGX_OPTION_NEGATABLE:
        slot->source = ARGX_SOURCE_DEFAULT;
        slot->on = argx_option_initial(option);
        break;
    case ARGX_OPTION_VALUE:
    case ARGX_OPTION_LIST:
    default:
        break;
    }
}

/*
 * Fills frame for command: allocates and initializes one slot for each of
 * its options and positionals. An empty slot array is not allocated.
 * Returns ARGX_ERROR_OUT_OF_MEMORY when an allocation fails; nothing is
 * left allocated then.
 */
static argx_error_t init_frame(argx_result_frame_t *frame, const argx_command_t *command)
{
    size_t i;

    frame->command = command;
    frame->options = NULL;
    frame->option_count = argx_command_option_count(command);
    frame->positionals = NULL;
    frame->positional_count = argx_command_positional_count(command);

    if (frame->option_count > 0)
    {
        frame->options =
            (argx_option_slot_t *)argx_calloc(frame->option_count, sizeof(*frame->options));
        if (frame->options == NULL)
        {
            return ARGX_ERROR_OUT_OF_MEMORY;
        }

        for (i = 0; i < frame->option_count; ++i)
        {
            init_option_slot(&frame->options[i], argx_command_option_at(command, i));
        }
    }

    if (frame->positional_count > 0)
    {
        frame->positionals = (argx_positional_slot_t *)argx_calloc(frame->positional_count,
                                                                   sizeof(*frame->positionals));
        if (frame->positionals == NULL)
        {
            argx_free(frame->options);
            return ARGX_ERROR_OUT_OF_MEMORY;
        }

        for (i = 0; i < frame->positional_count; ++i)
        {
            frame->positionals[i].source = ARGX_SOURCE_NONE;
            argx_vec_init(&frame->positionals[i].values, sizeof(argx_result_item_t));
        }
    }

    return ARGX_ERROR_NONE;
}

argx_error_t argx_result_push_command(argx_result_t *result, const argx_command_t *command)
{
    argx_result_frame_t frame;
    argx_error_t error;

    assert(result != NULL);
    assert(command != NULL);
    assert((result->frames.count > 0) || (command == result->root));
    assert((result->frames.count == 0) ||
           (argx_command_parent(command) == frame_at(result, result->frames.count - 1)->command));

    error = init_frame(&frame, command);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    error = argx_vec_push(&result->frames, &frame);
    if (error != ARGX_ERROR_NONE)
    {
        destroy_frame(&frame);
        return error;
    }

    return ARGX_ERROR_NONE;
}

/* ---- Option slots ------------------------------------------------------- */

/*
 * Returns the root of command's schema: the command that has no parent,
 * reached from command by following the parents.
 */
static const argx_command_t *root_of(const argx_command_t *command)
{
    const argx_command_t *parent = argx_command_parent(command);

    while (parent != NULL)
    {
        command = parent;
        parent = argx_command_parent(command);
    }

    return command;
}

/*
 * Returns option's slot in result, or NULL when option's command is not a
 * selected command of result.
 *
 * The frame is that of option's command, and the slot has the index that
 * option has in that command. A command of another schema is never
 * selected, so its options have no slot.
 */
static argx_option_slot_t *find_option_slot(const argx_result_t *result,
                                            const argx_option_t *option)
{
    const argx_command_t *command = argx_option_command(option);
    size_t i;
    size_t k;

    if (root_of(command) != result->root)
    {
        return NULL;
    }

    for (i = 0; i < result->frames.count; ++i)
    {
        argx_result_frame_t *frame = frame_at(result, i);

        if (frame->command != command)
        {
            continue;
        }

        for (k = 0; k < frame->option_count; ++k)
        {
            if (argx_command_option_at(command, k) == option)
            {
                return &frame->options[k];
            }
        }

        return NULL;
    }

    return NULL;
}

void argx_result_record_flag(argx_result_t *result, const argx_option_t *option)
{
    argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(argx_option_kind(option) == ARGX_OPTION_FLAG);

    slot = find_option_slot(result, option);
    assert(slot != NULL);

    slot->on = true;
    slot->source = ARGX_SOURCE_COMMAND_LINE;
}

void argx_result_record_terminal(argx_result_t *result, const argx_option_t *option)
{
    assert(result != NULL);
    assert(option != NULL);
    assert(argx_option_is_terminal(option));
    assert(argx_result_flag(result, option));
    assert(result->error == ARGX_ERROR_NONE);
    assert(result->terminal == NULL);

    result->terminal = option;
}

void argx_result_record_state(argx_result_t *result, const argx_option_t *option, bool on)
{
    argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(argx_option_kind(option) == ARGX_OPTION_NEGATABLE);

    slot = find_option_slot(result, option);
    assert(slot != NULL);

    slot->on = on;
    slot->source = ARGX_SOURCE_COMMAND_LINE;
}

void argx_result_record_count(argx_result_t *result, const argx_option_t *option)
{
    argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(argx_option_kind(option) == ARGX_OPTION_COUNT);

    slot = find_option_slot(result, option);
    assert(slot != NULL);

    slot->count += 1;
    slot->source = ARGX_SOURCE_COMMAND_LINE;
}

bool argx_result_flag(const argx_result_t *result, const argx_option_t *option)
{
    const argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_FLAG) ||
           (argx_option_kind(option) == ARGX_OPTION_NEGATABLE));

    slot = find_option_slot(result, option);
    if (slot == NULL)
    {
        /*
         * Not given: false for a flag, the initial state for a negatable
         * flag.
         */
        return argx_option_initial(option);
    }

    return slot->on;
}

size_t argx_result_count(const argx_result_t *result, const argx_option_t *option)
{
    const argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert(argx_option_kind(option) == ARGX_OPTION_COUNT);

    slot = find_option_slot(result, option);
    if (slot == NULL)
    {
        return 0;
    }

    return slot->count;
}

argx_source_t argx_result_option_source(const argx_result_t *result,
                                        const argx_option_t *option)
{
    const argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);

    slot = find_option_slot(result, option);
    if (slot == NULL)
    {
        return ARGX_SOURCE_NONE;
    }

    return slot->source;
}

/* ---- The values --------------------------------------------------------- */

/*
 * Returns positional's slot in result, or NULL when positional's command
 * is not a selected command of result. The slot is found the same way as
 * an option's; see find_option_slot().
 */
static argx_positional_slot_t *find_positional_slot(const argx_result_t *result,
                                                    const argx_positional_t *positional)
{
    const argx_command_t *command = argx_positional_command(positional);
    size_t i;
    size_t k;

    if (root_of(command) != result->root)
    {
        return NULL;
    }

    for (i = 0; i < result->frames.count; ++i)
    {
        argx_result_frame_t *frame = frame_at(result, i);

        if (frame->command != command)
        {
            continue;
        }

        for (k = 0; k < frame->positional_count; ++k)
        {
            if (argx_command_positional_at(command, k) == positional)
            {
                return &frame->positionals[k];
            }
        }

        return NULL;
    }

    return NULL;
}

/*
 * Allocates the area for a value of type, converts text into it and
 * writes the area to *out. text is passed to the value type unchanged.
 * This is the only place that converts a value.
 *
 * Returns ARGX_ERROR_NONE on success; the caller then owns the area and
 * the value in it (destroy_value()). On failure, *out is NULL and
 * nothing stays allocated:
 *
 * - ARGX_ERROR_OUT_OF_MEMORY when allocating the area fails. No area
 *   exists, and the value type is not called.
 * - What argx_type_convert() returned when the conversion fails. The area
 *   is freed without cleaning up the value.
 */
static argx_error_t convert_value(const argx_type_t *type, const char *text, void **out)
{
    void *value = argx_malloc(argx_type_value_size(type));
    argx_error_t error;

    *out = NULL;
    if (value == NULL)
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    error = argx_type_convert(type, text, value);
    if (error != ARGX_ERROR_NONE)
    {
        argx_free(value);
        return error;
    }

    *out = value;

    return ARGX_ERROR_NONE;
}

/* ---- The parse error ---------------------------------------------------- */

/*
 * Records the parse error failure in result: builds its message and, if
 * that succeeds, writes the error's fields. This is the only place that
 * gives a result a parse error.
 *
 * The message is built first, so running out of memory leaves result as
 * it was: no part of the error is recorded, and result still reports
 * ARGX_ERROR_NONE.
 *
 * Returns ARGX_ERROR_INVALID_COMMAND_LINE when the error was recorded
 * and ARGX_ERROR_OUT_OF_MEMORY when allocating the message fails.
 */
static argx_error_t record_error(argx_result_t *result, const argx_result_failure_t *failure)
{
    const argx_result_where_t *where = &failure->where;
    argx_message_parts_t parts;
    argx_error_t error;
    char *message;

    parts.kind = failure->kind;
    parts.source = where->source;
    parts.for_positional = failure->positional != NULL;
    parts.name_prefix = (where->name_prefix != NULL) ? where->name_prefix : "";
    parts.name = where->name;
    parts.name_length = where->name_length;
    parts.value = failure->value;
    parts.type_name = NULL;
    if (failure->kind == ARGX_PARSE_ERROR_INVALID_VALUE)
    {
        /*
         * The value type is that of the option or positional that the
         * value is for.
         */
        parts.type_name = argx_type_name((failure->positional != NULL) ?
                                         argx_positional_type(failure->positional) :
                                         argx_option_type(failure->option));
    }

    error = argx_message_build(&parts, &message);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    result->error = ARGX_ERROR_INVALID_COMMAND_LINE;
    result->error_kind = failure->kind;
    result->error_source = where->source;
    result->error_index = where->index;
    /* Only an error on the command line has an argument. */
    result->error_argument =
        (where->source == ARGX_SOURCE_COMMAND_LINE) ? argx_result_arg(result, where->index) : NULL;
    result->error_value = failure->value;
    result->error_command = failure->command;
    result->error_option = failure->option;
    result->error_positional = failure->positional;
    result->error_short_name = where->short_name;
    result->error_message = message;
    return ARGX_ERROR_INVALID_COMMAND_LINE;
}

argx_error_t argx_result_fail(argx_result_t *result, const argx_result_failure_t *failure)
{
    assert(result != NULL);
    assert(failure != NULL);
    assert(failure->kind != ARGX_PARSE_ERROR_NONE);
    assert(failure->command != NULL);
    assert(result->error == ARGX_ERROR_NONE);
    assert((failure->where.source != ARGX_SOURCE_COMMAND_LINE) || (failure->where.index < result->arg_count));
    assert((failure->kind != ARGX_PARSE_ERROR_INVALID_VALUE) || (failure->option != NULL) ||
           (failure->positional != NULL));

    return record_error(result, failure);
}

/*
 * What recording a value returns when convert_value() failed with error
 * for text at where. ARGX_ERROR_OUT_OF_MEMORY is returned as it is. Every
 * other failure becomes the parse error for a value that its value type
 * does not take, recorded in result as argx_result_fail() does. option or
 * positional is the one that the value is for; the other is NULL.
 */
static argx_error_t conversion_failure(argx_result_t *result, argx_error_t error, const char *text,
                                       const argx_result_where_t *where,
                                       const argx_option_t *option,
                                       const argx_positional_t *positional)
{
    argx_result_failure_t failure;

    if (error == ARGX_ERROR_OUT_OF_MEMORY)
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    failure.kind = ARGX_PARSE_ERROR_INVALID_VALUE;
    failure.where = *where;
    failure.value = text;
    failure.command =
        (positional != NULL) ? argx_positional_command(positional) : argx_option_command(option);
    failure.option = option;
    failure.positional = positional;

    return argx_result_fail(result, &failure);
}

/*
 * Appends value, of type, at the argument index to items, an array of
 * argx_result_item_t. On failure, value is cleaned up and
 * ARGX_ERROR_OUT_OF_MEMORY is returned; items is unchanged.
 */
static argx_error_t push_item(argx_vec_t *items, const argx_type_t *type, void *value, size_t index)
{
    argx_result_item_t item;

    item.value = value;
    item.index = index;
    if (argx_vec_push(items, &item) != ARGX_ERROR_NONE)
    {
        /* The value is not kept, so it is cleaned up here. */
        destroy_value(type, value);
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    return ARGX_ERROR_NONE;
}

argx_error_t argx_result_record_option_value(argx_result_t *result, const argx_option_t *option,
                                             const char *text, const argx_result_where_t *where)
{
    const argx_type_t *type;
    argx_option_slot_t *slot;
    argx_error_t error;
    void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(text != NULL);
    assert(where != NULL);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));

    slot = find_option_slot(result, option);
    assert(slot != NULL);

    /*
     * The new value is converted before the old one is touched: if the
     * conversion fails, the old value stays.
     */
    type = argx_option_type(option);
    error = convert_value(type, text, &value);
    if (error != ARGX_ERROR_NONE)
    {
        return conversion_failure(result, error, text, where, option, NULL);
    }

    if (argx_option_kind(option) == ARGX_OPTION_LIST)
    {
        error = push_item(&slot->items, type, value, where->index);
        if (error != ARGX_ERROR_NONE)
        {
            return error;
        }
    }
    else
    {
        if (slot->value != NULL)
        {
            destroy_value(type, slot->value);
        }

        slot->value = value;
        slot->index = where->index;
    }

    slot->source = where->source;

    return ARGX_ERROR_NONE;
}

/*
 * Copies text, replaces each separator in the copy with '\0', and keeps
 * the copy in result's texts, where it stays until result is destroyed.
 * Writes the copy to *out. Returns ARGX_ERROR_OUT_OF_MEMORY when an
 * allocation fails; nothing is kept then.
 */
static argx_error_t keep_split_copy(argx_result_t *result, const char *text, char separator,
                                    char **out)
{
    const size_t length = strlen(text);
    char *copy;
    size_t i;

    /* A text from an argument has length < SIZE_MAX, so this cannot wrap. */
    copy = (char *)argx_malloc(length + 1);
    if (copy == NULL)
    {
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    for (i = 0; i < length; ++i)
    {
        copy[i] = (text[i] == separator) ? '\0' : text[i];
    }
    copy[length] = '\0';

    if (argx_vec_push(&result->texts, &copy) != ARGX_ERROR_NONE)
    {
        argx_free(copy);
        return ARGX_ERROR_OUT_OF_MEMORY;
    }

    *out = copy;

    return ARGX_ERROR_NONE;
}

argx_error_t argx_result_record_option_items(argx_result_t *result, const argx_option_t *option,
                                             const char *text, const argx_result_where_t *where)
{
    const argx_type_t *type;
    argx_option_slot_t *slot;
    argx_vec_t converted;
    argx_error_t error = ARGX_ERROR_NONE;
    const char *item;
    const char *end;
    char separator;
    char *copy;
    size_t i;

    assert(result != NULL);
    assert(option != NULL);
    assert(text != NULL);
    assert(where != NULL);
    assert(argx_option_kind(option) == ARGX_OPTION_LIST);
    assert(where->source == ARGX_SOURCE_COMMAND_LINE);

    separator = argx_option_separator(option);
    if (separator == '\0')
    {
        return argx_result_record_option_value(result, option, text, where);
    }

    slot = find_option_slot(result, option);
    assert(slot != NULL);

    error = keep_split_copy(result, text, separator, &copy);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    /*
     * Every item is converted before any is kept, so a parse error leaves
     * the slot as it was. The copy has as many '\0' as text has
     * separators, plus its own end.
     */
    type = argx_option_type(option);
    argx_vec_init(&converted, sizeof(argx_result_item_t));
    end = copy + strlen(text);
    for (item = copy; item < end; item += strlen(item) + 1)
    {
        void *value;

        error = convert_value(type, item, &value);
        if (error != ARGX_ERROR_NONE)
        {
            error = conversion_failure(result, error, item, where, option, NULL);
            break;
        }

        error = push_item(&converted, type, value, where->index);
        if (error != ARGX_ERROR_NONE)
        {
            break;
        }
    }

    if (error != ARGX_ERROR_NONE)
    {
        destroy_items(type, &converted);
        return error;
    }

    for (i = 0; i < converted.count; ++i)
    {
        const argx_result_item_t *next = (const argx_result_item_t *)argx_vec_at(&converted, i);

        error = push_item(&slot->items, type, next->value, next->index);
        if (error != ARGX_ERROR_NONE)
        {
            /*
             * push_item() cleaned up the value at i; the ones after it are
             * not kept either.
             */
            for (++i; i < converted.count; ++i)
            {
                destroy_value(type, ((argx_result_item_t *)argx_vec_at(&converted, i))->value);
            }
            break;
        }
    }

    argx_vec_destroy(&converted);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    slot->source = where->source;

    return ARGX_ERROR_NONE;
}

argx_error_t argx_result_record_positional_value(argx_result_t *result,
                                                 const argx_positional_t *positional,
                                                 const char *text, const argx_result_where_t *where)
{
    const argx_type_t *type;
    argx_positional_slot_t *slot;
    argx_error_t error;
    void *value;

    assert(result != NULL);
    assert(positional != NULL);
    assert(text != NULL);
    assert(where != NULL);

    slot = find_positional_slot(result, positional);
    assert(slot != NULL);

    type = argx_positional_type(positional);
    error = convert_value(type, text, &value);
    if (error != ARGX_ERROR_NONE)
    {
        return conversion_failure(result, error, text, where, NULL, positional);
    }

    error = push_item(&slot->values, type, value, where->index);
    if (error != ARGX_ERROR_NONE)
    {
        return error;
    }

    slot->source = where->source;

    return ARGX_ERROR_NONE;
}

/*
 * Takes back the default values in frame's slots; see
 * argx_result_discard_defaults().
 */
static void discard_frame_defaults(argx_result_frame_t *frame)
{
    size_t i;

    for (i = 0; i < frame->option_count; ++i)
    {
        argx_option_slot_t *slot = &frame->options[i];
        const argx_type_t *type;

        /*
         * A flag, count or negatable flag that was not given also has the
         * source of a default value, but never a value or an item.
         */
        if ((slot->source != ARGX_SOURCE_DEFAULT) ||
            ((slot->value == NULL) && (slot->items.count == 0)))
        {
            continue;
        }

        type = argx_option_type(argx_command_option_at(frame->command, i));
        if (slot->value != NULL)
        {
            destroy_value(type, slot->value);
            slot->value = NULL;
        }

        /* The slot is now as it was when the frame was made. */
        destroy_items(type, &slot->items);
        slot->index = 0;
        slot->source = ARGX_SOURCE_NONE;
    }

    for (i = 0; i < frame->positional_count; ++i)
    {
        argx_positional_slot_t *slot = &frame->positionals[i];

        if (slot->source != ARGX_SOURCE_DEFAULT)
        {
            continue;
        }

        /* The slot is now as it was when the frame was made. */
        destroy_items(argx_positional_type(argx_command_positional_at(frame->command, i)),
                      &slot->values);
        slot->source = ARGX_SOURCE_NONE;
    }
}

void argx_result_discard_defaults(argx_result_t *result)
{
    size_t i;

    assert(result != NULL);

    for (i = 0; i < result->frames.count; ++i)
    {
        discard_frame_defaults(frame_at(result, i));
    }
}

/*
 * Returns the area holding option's value in result, or NULL when option
 * has no value or its command is not a selected command of result.
 */
static const void *option_value(const argx_result_t *result, const argx_option_t *option)
{
    const argx_option_slot_t *slot = find_option_slot(result, option);

    if (slot == NULL)
    {
        return NULL;
    }

    return slot->value;
}

bool argx_result_option_string(const argx_result_t *result, const argx_option_t *option,
                               const char **out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert(argx_option_kind(option) == ARGX_OPTION_VALUE);
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_STRING);

    value = option_value(result, option);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const char *const *)value;

    return true;
}

bool argx_result_option_int(const argx_result_t *result, const argx_option_t *option, int64_t *out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert(argx_option_kind(option) == ARGX_OPTION_VALUE);
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_INT);

    value = option_value(result, option);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const int64_t *)value;

    return true;
}

bool argx_result_option_float(const argx_result_t *result, const argx_option_t *option, double *out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert(argx_option_kind(option) == ARGX_OPTION_VALUE);
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_FLOAT);

    value = option_value(result, option);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const double *)value;

    return true;
}

bool argx_result_option_choice(const argx_result_t *result, const argx_option_t *option, size_t *out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert(argx_option_kind(option) == ARGX_OPTION_VALUE);
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_CHOICE);

    value = option_value(result, option);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const size_t *)value;

    return true;
}

const void *argx_result_option_value(const argx_result_t *result, const argx_option_t *option)
{
    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert(argx_option_kind(option) == ARGX_OPTION_VALUE);

    return option_value(result, option);
}

/*
 * Returns the item at index of option, an option that takes a value, in
 * result: its value and the index of its argument. An option with a value
 * has one item when it has a value. Returns false when there is no item
 * at index or option's command is not a selected command of result.
 */
static bool option_item(const argx_result_t *result, const argx_option_t *option, size_t index,
                        argx_result_item_t *out)
{
    const argx_option_slot_t *slot = find_option_slot(result, option);

    if (slot == NULL)
    {
        return false;
    }

    if (argx_option_kind(option) == ARGX_OPTION_LIST)
    {
        if (index >= slot->items.count)
        {
            return false;
        }

        *out = *(const argx_result_item_t *)argx_vec_at(&slot->items, index);
        return true;
    }

    if ((index != 0) || (slot->value == NULL))
    {
        return false;
    }

    out->value = slot->value;
    out->index = slot->index;

    return true;
}

/* Returns the area of the item at index of option, or NULL; see option_item(). */
static const void *option_value_at(const argx_result_t *result, const argx_option_t *option,
                                   size_t index)
{
    argx_result_item_t item;

    if (!option_item(result, option, index, &item))
    {
        return NULL;
    }

    return item.value;
}

size_t argx_result_option_value_count(const argx_result_t *result, const argx_option_t *option)
{
    const argx_option_slot_t *slot;

    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));

    slot = find_option_slot(result, option);
    if (slot == NULL)
    {
        return 0;
    }

    if (argx_option_kind(option) == ARGX_OPTION_LIST)
    {
        return slot->items.count;
    }

    return (slot->value != NULL) ? 1U : 0U;
}

bool argx_result_option_string_at(const argx_result_t *result, const argx_option_t *option,
                                  size_t index, const char **out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_STRING);

    value = option_value_at(result, option, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const char *const *)value;

    return true;
}

bool argx_result_option_int_at(const argx_result_t *result, const argx_option_t *option,
                               size_t index, int64_t *out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_INT);

    value = option_value_at(result, option, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const int64_t *)value;

    return true;
}

bool argx_result_option_float_at(const argx_result_t *result, const argx_option_t *option,
                                 size_t index, double *out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_FLOAT);

    value = option_value_at(result, option, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const double *)value;

    return true;
}

bool argx_result_option_choice_at(const argx_result_t *result, const argx_option_t *option,
                                  size_t index, size_t *out)
{
    const void *value;

    assert(result != NULL);
    assert(option != NULL);
    assert(out != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));
    assert(argx_type_kind(argx_option_type(option)) == ARGX_TYPE_CHOICE);

    value = option_value_at(result, option, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const size_t *)value;

    return true;
}

const void *argx_result_option_value_at(const argx_result_t *result, const argx_option_t *option,
                                        size_t index)
{
    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));

    return option_value_at(result, option, index);
}

size_t argx_result_option_arg_index(const argx_result_t *result, const argx_option_t *option,
                                    size_t index)
{
    argx_result_item_t item;

    assert(result != NULL);
    assert(option != NULL);
    assert(root_of(argx_option_command(option)) == result->root);
    assert((argx_option_kind(option) == ARGX_OPTION_VALUE) ||
           (argx_option_kind(option) == ARGX_OPTION_LIST));

    if (!option_item(result, option, index, &item))
    {
        return 0;
    }

    return item.index;
}

size_t argx_result_positional_count(const argx_result_t *result, const argx_positional_t *positional)
{
    const argx_positional_slot_t *slot;

    assert(result != NULL);
    assert(positional != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);

    slot = find_positional_slot(result, positional);
    if (slot == NULL)
    {
        return 0;
    }

    return slot->values.count;
}

argx_source_t argx_result_positional_source(const argx_result_t *result,
                                            const argx_positional_t *positional)
{
    const argx_positional_slot_t *slot;

    assert(result != NULL);
    assert(positional != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);

    slot = find_positional_slot(result, positional);
    if (slot == NULL)
    {
        return ARGX_SOURCE_NONE;
    }

    return slot->source;
}

/*
 * Returns the area holding positional's value at index in result, or NULL
 * when there is no value at index or positional's command is not a
 * selected command of result.
 */
static const void *positional_value(const argx_result_t *result, const argx_positional_t *positional,
                                    size_t index)
{
    const argx_positional_slot_t *slot = find_positional_slot(result, positional);

    if ((slot == NULL) || (index >= slot->values.count))
    {
        return NULL;
    }

    return ((const argx_result_item_t *)argx_vec_at(&slot->values, index))->value;
}

bool argx_result_positional_string(const argx_result_t *result, const argx_positional_t *positional,
                                   size_t index, const char **out)
{
    const void *value;

    assert(result != NULL);
    assert(positional != NULL);
    assert(out != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);
    assert(argx_type_kind(argx_positional_type(positional)) == ARGX_TYPE_STRING);

    value = positional_value(result, positional, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const char *const *)value;

    return true;
}

bool argx_result_positional_int(const argx_result_t *result, const argx_positional_t *positional,
                                size_t index, int64_t *out)
{
    const void *value;

    assert(result != NULL);
    assert(positional != NULL);
    assert(out != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);
    assert(argx_type_kind(argx_positional_type(positional)) == ARGX_TYPE_INT);

    value = positional_value(result, positional, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const int64_t *)value;

    return true;
}

bool argx_result_positional_float(const argx_result_t *result, const argx_positional_t *positional,
                                  size_t index, double *out)
{
    const void *value;

    assert(result != NULL);
    assert(positional != NULL);
    assert(out != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);
    assert(argx_type_kind(argx_positional_type(positional)) == ARGX_TYPE_FLOAT);

    value = positional_value(result, positional, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const double *)value;

    return true;
}

bool argx_result_positional_choice(const argx_result_t *result, const argx_positional_t *positional,
                                   size_t index, size_t *out)
{
    const void *value;

    assert(result != NULL);
    assert(positional != NULL);
    assert(out != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);
    assert(argx_type_kind(argx_positional_type(positional)) == ARGX_TYPE_CHOICE);

    value = positional_value(result, positional, index);
    if (value == NULL)
    {
        return false;
    }

    *out = *(const size_t *)value;

    return true;
}

const void *argx_result_positional_value(const argx_result_t *result,
                                         const argx_positional_t *positional, size_t index)
{
    assert(result != NULL);
    assert(positional != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);

    return positional_value(result, positional, index);
}

size_t argx_result_positional_arg_index(const argx_result_t *result,
                                        const argx_positional_t *positional, size_t index)
{
    const argx_positional_slot_t *slot;

    assert(result != NULL);
    assert(positional != NULL);
    assert(root_of(argx_positional_command(positional)) == result->root);

    slot = find_positional_slot(result, positional);
    if ((slot == NULL) || (index >= slot->values.count))
    {
        return 0;
    }

    return ((const argx_result_item_t *)argx_vec_at(&slot->values, index))->index;
}

/* ---- The outcome and the selected commands ------------------------------ */

argx_error_t argx_result_error(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error;
}

size_t argx_result_command_count(const argx_result_t *result)
{
    assert(result != NULL);

    return result->frames.count;
}

const argx_command_t *argx_result_command_at(const argx_result_t *result, size_t index)
{
    assert(result != NULL);
    assert(index < result->frames.count);

    return frame_at(result, index)->command;
}

const argx_command_t *argx_result_command(const argx_result_t *result)
{
    assert(result != NULL);
    assert(result->frames.count > 0);

    return frame_at(result, result->frames.count - 1)->command;
}

const argx_option_t *argx_result_terminal_option(const argx_result_t *result)
{
    assert(result != NULL);

    return result->terminal;
}

/* ---- Parse error details ------------------------------------------------ */

/*
 * The fields below hold their "none" values until record_error() writes
 * them, so a result without a parse error needs no special case.
 */

argx_parse_error_kind_t argx_result_error_kind(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_kind;
}

const char *argx_result_error_message(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_message;
}

const char *argx_result_error_argument(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_argument;
}

const char *argx_result_error_value(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_value;
}

argx_source_t argx_result_error_source(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_source;
}

size_t argx_result_error_index(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_index;
}

const argx_command_t *argx_result_error_command(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_command;
}

const argx_option_t *argx_result_error_option(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_option;
}

const argx_positional_t *argx_result_error_positional(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_positional;
}

char argx_result_error_short_name(const argx_result_t *result)
{
    assert(result != NULL);

    return result->error_short_name;
}
