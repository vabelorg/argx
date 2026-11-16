# Copyright (c) 2026 The ArgX Authors
# SPDX-License-Identifier: MIT
#
# The example "measure": a help in Japanese, measured with wcwidth(). It needs
# POSIX, so it is not built on Windows. The display width depends on the locale
# of the test, so the tests check only what does not: the usage line, the
# lines without an aligned description, and the line breaks of the author.

argx_add_example(measure)
argx_add_example_test(measure help
    "^Usage: measure \\[OPTIONS\\] <入力>\r?\n\r?\n画像を変換します。\r?\n\r?\nArguments:\r?\n  <入力> +変換する画像のファイル\r?\n\r?\nOptions:\r?\n  -h, --help +このヘルプを表示します。\r?\n  -o, --output <出力> +変換した画像を書き込むファイル。\r?\n +省略すると、入力と同じ場所に書き込みます。\r?\n$"
    ARGS --help)
argx_add_example_test(measure input
    "^photo\\.png\r?\n$"
    ARGS photo.png)
argx_add_example_failure_test(measure missing-input-status)
