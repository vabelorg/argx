# Copyright (c) 2026 The ArgX Authors
# SPDX-License-Identifier: MIT
#
# ArgXChangelog.cmake - reads and updates CHANGELOG.md, which follows Keep a
# Changelog 1.1.0 (https://keepachangelog.com/en/1.1.0/).
#
# The release scripts include this module and run with "cmake -P". It needs
# nothing that project() sets, and touches no file, network, git or GitHub:
# every function takes the text of the changelog and returns a result.
#
# The accepted form
# -----------------
#
#   # Changelog
#   <preamble: any lines that are not "## " headings or link definitions>
#   ## [Unreleased]
#   <body>
#   ## [<version>] - <YYYY-MM-DD>        (0 or more, newest first)
#   <body>
#   <link definitions: lines "[<label>]: <url>", at the end>
#
#   - The first line is "# Changelog".
#   - "## [Unreleased]" appears exactly once and is the first "## " heading.
#   - Every other "## " heading is "## [<version>] - <YYYY-MM-DD>", where
#     <version> is valid for ArgXSemver.cmake. No version appears twice, and
#     each is lower, by SemVer 2.0.0 precedence, than the one above it.
#   - In a body, a "### " heading is one of the categories of Keep a Changelog:
#     Added, Changed, Deprecated, Removed, Fixed or Security. An entry is a
#     line that starts with "- " or "* "; indented lines (nested items and
#     continuation lines) are not entries.
#   - From the first link definition (a line that starts with "[<label>]: ")
#     on, there are only link definitions and blank lines, and exactly one of
#     them is "[Unreleased]: ".
#   - Lines end with LF or CRLF. The text written back keeps the line endings
#     of the lines it keeps, and the lines it adds end as the text does (CRLF
#     when the text holds one, LF otherwise).
#
# The body of a section runs from the line after its heading to the line
# before the next "## " heading or the first link definition, without the
# blank lines at its start and at its end.
#
#   argx_changelog_parse(<text> <prefix>)
#
# Checks <text> against the accepted form and sets, in the scope of the caller:
#
#   <prefix>_VALID               TRUE or FALSE.
#   <prefix>_ERROR               "" when valid; otherwise an English sentence
#                                that starts with "line <n>: " when the problem
#                                is on a line.
#   <prefix>_VERSIONS            The released versions as a list, newest first,
#                                as in the file; "" when there is none.
#   <prefix>_UNRELEASED_ENTRIES  The number of entries under [Unreleased].
#
# When the text is not valid, <prefix>_VERSIONS and
# <prefix>_UNRELEASED_ENTRIES are "".
#
#   argx_changelog_promote(<text> <version> <date> <repo-url> <out-var>)
#
# Sets <out-var> to <text> with the body of [Unreleased] moved to a new section
# "## [<version>] - <date>" right below "## [Unreleased]", which is left empty,
# and the link definitions updated:
#
#   [Unreleased]: <repo-url>/compare/v<version>...HEAD
#   [<version>]: <repo-url>/compare/v<previous>...v<version>
#
# where <previous> is the newest version already in the file. For the first
# version, the second link is "<repo-url>/releases/tag/v<version>". The new
# link comes right after the "[Unreleased]: " line, which it replaces. Every
# other line is kept as it is. <repo-url> is "https://github.com/<owner>/<repo>"
# (a slash at its end is dropped).
#
# Stops with a FATAL_ERROR when <version> or <date> (YYYY-MM-DD) is not valid,
# <repo-url> is empty, the text is not valid, [Unreleased] has no entry,
# <version> is already listed, or <version> is not higher than the newest
# version listed (the result would not be valid).
#
#   argx_changelog_notes(<text> <version> <repo-url> <out-var>)
#
# Sets <out-var> to the body of the section of <version>, without its heading
# and without a line ending at its end. The lines keep the line endings of the
# text. When <version> has no pre-release identifiers and the versions right
# below it in the file are pre-releases of the same <major>.<minor>.<patch>,
# a blank line and this sentence follow the body, with the pre-releases in the
# order of the file:
#
#   This release also includes the changes in the pre-releases
#   [v<pre1>](<repo-url>/releases/tag/v<pre1>), [v<pre2>](...).
#
# Stops with a FATAL_ERROR when <repo-url> is empty, the text is not valid or
# it has no section for <version>.
#
#   argx_changelog_check_version(<text> <version> <base> <prefix>)
#
# Checks that the entries under [Unreleased] are written as the release needs
# them and allow <version>, the version to release, after <base>, the highest
# release that is not a pre-release ("" when there is none). A pre-release is
# checked as the version it leads to: 0.2.0-rc.1 as 0.2.0. A breaking change
# is an entry that starts with "**BREAKING**: " (see docs/development/
# releasing.md#keeping-the-changelog). Sets, in the scope of the caller:
#
#   <prefix>_VALID     TRUE or FALSE.
#   <prefix>_ERROR     "" when valid; otherwise English sentences, which start
#                      with "line <n>: " when the problem is the form of an
#                      entry.
#   <prefix>_KIND      "major", "minor" or "patch" when <version> raises that
#                      part of <base>; "first" when <base> is "".
#   <prefix>_BREAKING  The number of **BREAKING** entries under [Unreleased].
#
# The first problem found is reported, in this order:
#
#   1. Every entry is under a category heading.
#   2. No entry starts with a marker close to "**BREAKING**: " but not equal
#      to it, such as "**Breaking**:" or "BREAKING:".
#   3. A **BREAKING** entry is under Changed or Removed.
#   4. Every category heading has at least one entry.
#   5. Unless <base> is "": a patch release has Fixed and Security entries
#      only (no **BREAKING** entry, then); from 1.0 on (<base> 1.0.0 or
#      higher), a **BREAKING** entry needs a major release. The error names
#      the entry and the lowest version that would be allowed.
#
# Stops with a FATAL_ERROR when <version> is not valid, <base> is not valid or
# is a pre-release, or the text is not valid.
#
# Every FATAL_ERROR starts with "error: " and holds the reason in English.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ArgXSemver.cmake")

# The functions keep the policies in effect here, whatever the caller has set
# (a "cmake -P" script may have set none).
cmake_policy(PUSH)
cmake_policy(VERSION 3.21)

# The lines of the text are kept in a CMake list. Semicolons, square brackets
# and backslashes change how CMake splits a list, so they are replaced by
# control characters while the text is a list, and put back line by line.
# Sets the codes in the scope of the calling function.
macro(_argx_changelog_codes)
    string(ASCII 1 _argx_changelog_semicolon)
    string(ASCII 2 _argx_changelog_open)
    string(ASCII 3 _argx_changelog_close)
    string(ASCII 4 _argx_changelog_backslash)
endmacro()

# Puts back the characters that the list of lines holds as codes.
macro(_argx_changelog_decode var)
    string(REPLACE "${_argx_changelog_semicolon}" ";" ${var} "${${var}}")
    string(REPLACE "${_argx_changelog_open}" "[" ${var} "${${var}}")
    string(REPLACE "${_argx_changelog_close}" "]" ${var} "${${var}}")
    string(REPLACE "${_argx_changelog_backslash}" "\\" ${var} "${${var}}")
endmacro()

# Sets <out-var> to the line at <index> of the list <lines>, as in the text
# (with its carriage return, if any).
function(_argx_changelog_raw_line lines index out_var)
    _argx_changelog_codes()
    list(GET lines ${index} line)
    _argx_changelog_decode(line)
    set(${out_var} "${line}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to the line at <index> without its carriage return.
function(_argx_changelog_line lines index out_var)
    _argx_changelog_raw_line("${lines}" ${index} line)
    string(REGEX REPLACE "\r$" "" line "${line}")
    set(${out_var} "${line}" PARENT_SCOPE)
endfunction()

# Narrows the lines from <first-var> to <last-var> (both indexes, inclusive) so
# that they neither start nor end with a blank line. The range is empty when
# <first-var> ends up greater than <last-var>.
function(_argx_changelog_trim lines first_var last_var)
    set(first ${${first_var}})
    set(last ${${last_var}})
    while(first LESS_EQUAL last)
        _argx_changelog_line("${lines}" ${first} line)
        if(NOT line MATCHES "^[ \t]*$")
            break()
        endif()
        math(EXPR first "${first} + 1")
    endwhile()
    while(last GREATER_EQUAL first)
        _argx_changelog_line("${lines}" ${last} line)
        if(NOT line MATCHES "^[ \t]*$")
            break()
        endif()
        math(EXPR last "${last} - 1")
    endwhile()
    set(${first_var} ${first} PARENT_SCOPE)
    set(${last_var} ${last} PARENT_SCOPE)
endfunction()

# Sets <out-var> to the mark of the entry <line>: "breaking" when it starts
# with the marker "**BREAKING**: ", "malformed" when it starts with something
# close to it ("**Breaking**:", "**BREAKING:**", "BREAKING:", "__BREAKING__"),
# "none" otherwise. An entry such as "- Breaking up long lines ..." is "none":
# without emphasis, only "breaking" followed by a colon is close to the marker.
function(_argx_changelog_mark line out_var)
    string(TOLOWER "${line}" lower)
    if(line MATCHES "^[-*] \\*\\*BREAKING\\*\\*: ")
        set(mark breaking)
    elseif(lower MATCHES "^[-*] [*_]*breaking[*_]*:" OR lower MATCHES "^[-*] [*_]+breaking")
        set(mark malformed)
    else()
        set(mark none)
    endif()
    set(${out_var} ${mark} PARENT_SCOPE)
endfunction()

# Checks the text and sets, besides the variables of argx_changelog_parse
# (which are set whether or not the text is valid):
#
#   <prefix>_NL                    The line ending of added lines.
#   <prefix>_LINES                 The lines, as a list in the coded form.
#   <prefix>_COUNT                 The number of elements of the list. When
#                                  the text ends with a line ending, the last
#                                  element is the empty text after it.
#   <prefix>_UNRELEASED_LINE       The index of "## [Unreleased]".
#   <prefix>_VERSION_LINES         The index of the heading of each version.
#   <prefix>_LINKS_LINE            The index of the first link definition.
#   <prefix>_UNRELEASED_LINK_LINE  The index of the "[Unreleased]: " line.
#   <prefix>_ENTRY_NUMBERS         The line number (from 1) of each entry
#                                  under [Unreleased].
#   <prefix>_ENTRY_CATEGORIES      The category of each of these entries, or
#                                  "-" for an entry above every category
#                                  heading.
#   <prefix>_ENTRY_MARKS           The mark of each of these entries, as
#                                  _argx_changelog_mark() sets it.
#   <prefix>_EMPTY_HEADINGS        The line numbers of the category headings
#                                  under [Unreleased] with no entry.
function(_argx_changelog_scan text prefix)
    _argx_changelog_codes()
    set(problem "")

    foreach(code IN ITEMS semicolon open close backslash)
        string(FIND "${text}" "${_argx_changelog_${code}}" pos)
        if(NOT pos EQUAL -1)
            set(problem "the text holds a control character (U+0001 to U+0004).")
        endif()
    endforeach()

    set(nl "\n")
    string(FIND "${text}" "\r\n" pos)
    if(NOT pos EQUAL -1)
        set(nl "\r\n")
    endif()

    set(lines "${text}")
    string(REPLACE "\\" "${_argx_changelog_backslash}" lines "${lines}")
    string(REPLACE ";" "${_argx_changelog_semicolon}" lines "${lines}")
    string(REPLACE "[" "${_argx_changelog_open}" lines "${lines}")
    string(REPLACE "]" "${_argx_changelog_close}" lines "${lines}")
    string(REPLACE "\n" ";" lines "${lines}")
    list(LENGTH lines count)

    set(state "title")
    set(versions "")
    set(version_lines "")
    set(entries 0)
    set(unreleased_line -1)
    set(links_line -1)
    set(unreleased_link_line -1)
    set(unreleased_links 0)
    set(categories Added Changed Deprecated Removed Fixed Security)
    # The entries of [Unreleased], as three lists of the same length, and the
    # category headings under it that have no entry.
    set(category "")
    set(entry_numbers "")
    set(entry_categories "")
    set(entry_marks "")
    set(empty_headings "")
    set(pending_heading "")

    if(problem STREQUAL "" AND count EQUAL 0)
        set(problem "line 1: the first line must be \"# Changelog\".")
    endif()
    if(problem STREQUAL "")
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            if(NOT problem STREQUAL "")
                break()
            endif()
            _argx_changelog_line("${lines}" ${index} line)
            math(EXPR number "${index} + 1")
            set(where "line ${number}: ")
            set(is_link FALSE)
            if(line MATCHES "^\\[[^]]+\\]: ")
                set(is_link TRUE)
            endif()
            set(is_unreleased_link FALSE)
            if(line MATCHES "^\\[Unreleased\\]: ")
                set(is_unreleased_link TRUE)
            endif()
            # A category heading of [Unreleased] ends at the next "### " or
            # "## " heading, or at the first link definition.
            if(state STREQUAL "unreleased" AND NOT pending_heading STREQUAL ""
                    AND (is_link OR line MATCHES "^##"))
                list(APPEND empty_headings ${pending_heading})
                set(pending_heading "")
            endif()

            if(state STREQUAL "title")
                if(NOT line STREQUAL "# Changelog")
                    set(problem "${where}the first line must be \"# Changelog\".")
                endif()
                set(state "preamble")
            elseif(state STREQUAL "links")
                if(is_link)
                    if(is_unreleased_link)
                        math(EXPR unreleased_links "${unreleased_links} + 1")
                        set(unreleased_link_line ${index})
                    endif()
                elseif(NOT line MATCHES "^[ \t]*$")
                    set(problem "${where}only link definitions and blank lines may follow \
the first link definition (line ${links_line_number}), but this line is \"${line}\".")
                endif()
            elseif(is_link)
                if(state STREQUAL "preamble")
                    set(problem "${where}a link definition comes before \"## [Unreleased]\"; \
link definitions belong at the end.")
                else()
                    set(state "links")
                    set(links_line ${index})
                    set(links_line_number ${number})
                    if(is_unreleased_link)
                        math(EXPR unreleased_links "${unreleased_links} + 1")
                        set(unreleased_link_line ${index})
                    endif()
                endif()
            elseif(line MATCHES "^## ")
                if(line STREQUAL "## [Unreleased]")
                    if(state STREQUAL "preamble")
                        set(state "unreleased")
                        set(unreleased_line ${index})
                    else()
                        set(problem "${where}\"## [Unreleased]\" appears more than once.")
                    endif()
                elseif(state STREQUAL "preamble")
                    set(problem "${where}the first \"## \" heading must be \"## [Unreleased]\", \
not \"${line}\".")
                elseif(line MATCHES "^## \\[([^]]*)\\] - [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
                    set(version "${CMAKE_MATCH_1}")
                    argx_semver_parse("${version}" v)
                    if(NOT v_VALID)
                        set(problem "${where}${v_ERROR}")
                    elseif(version IN_LIST versions)
                        set(problem "${where}the version ${version} is listed twice.")
                    else()
                        if(NOT versions STREQUAL "")
                            list(GET versions -1 above)
                            argx_semver_compare("${above}" "${version}" order)
                            if(NOT order EQUAL 1)
                                set(problem "${where}the version ${version} is not lower than \
${above}, the version above it; versions must be listed newest first.")
                            endif()
                        endif()
                        list(APPEND versions "${version}")
                        list(APPEND version_lines ${index})
                        set(state "version")
                    endif()
                else()
                    set(problem "${where}the heading \"${line}\" is not of the form \
\"## [<version>] - <YYYY-MM-DD>\".")
                endif()
            elseif(line MATCHES "^### ")
                if(NOT state STREQUAL "preamble")
                    string(SUBSTRING "${line}" 4 -1 category)
                    if(NOT category IN_LIST categories)
                        set(problem "${where}\"${line}\" is not a category of Keep a Changelog \
(Added, Changed, Deprecated, Removed, Fixed, Security).")
                    elseif(state STREQUAL "unreleased")
                        set(pending_heading ${number})
                    endif()
                endif()
            elseif(line MATCHES "^[-*] ")
                if(state STREQUAL "unreleased")
                    math(EXPR entries "${entries} + 1")
                    set(pending_heading "")
                    _argx_changelog_mark("${line}" mark)
                    list(APPEND entry_numbers ${number})
                    if(category STREQUAL "")
                        list(APPEND entry_categories "-")
                    else()
                        list(APPEND entry_categories "${category}")
                    endif()
                    list(APPEND entry_marks ${mark})
                endif()
            endif()
        endforeach()
        if(state STREQUAL "unreleased" AND NOT pending_heading STREQUAL "")
            list(APPEND empty_headings ${pending_heading})
        endif()
    endif()

    if(problem STREQUAL "")
        if(unreleased_line EQUAL -1)
            set(problem "there is no \"## [Unreleased]\" heading.")
        elseif(NOT unreleased_links EQUAL 1)
            set(problem "there must be exactly one \"[Unreleased]: \" link definition; \
there are ${unreleased_links}.")
        endif()
    endif()

    if(problem STREQUAL "")
        set(${prefix}_VALID TRUE PARENT_SCOPE)
        set(${prefix}_ERROR "" PARENT_SCOPE)
        set(${prefix}_VERSIONS "${versions}" PARENT_SCOPE)
        set(${prefix}_UNRELEASED_ENTRIES ${entries} PARENT_SCOPE)
    else()
        set(${prefix}_VALID FALSE PARENT_SCOPE)
        set(${prefix}_ERROR "${problem}" PARENT_SCOPE)
        set(${prefix}_VERSIONS "" PARENT_SCOPE)
        set(${prefix}_UNRELEASED_ENTRIES "" PARENT_SCOPE)
    endif()
    set(${prefix}_NL "${nl}" PARENT_SCOPE)
    set(${prefix}_LINES "${lines}" PARENT_SCOPE)
    set(${prefix}_COUNT ${count} PARENT_SCOPE)
    set(${prefix}_UNRELEASED_LINE ${unreleased_line} PARENT_SCOPE)
    set(${prefix}_VERSION_LINES "${version_lines}" PARENT_SCOPE)
    set(${prefix}_LINKS_LINE ${links_line} PARENT_SCOPE)
    set(${prefix}_UNRELEASED_LINK_LINE ${unreleased_link_line} PARENT_SCOPE)
    set(${prefix}_ENTRY_NUMBERS "${entry_numbers}" PARENT_SCOPE)
    set(${prefix}_ENTRY_CATEGORIES "${entry_categories}" PARENT_SCOPE)
    set(${prefix}_ENTRY_MARKS "${entry_marks}" PARENT_SCOPE)
    set(${prefix}_EMPTY_HEADINGS "${empty_headings}" PARENT_SCOPE)
endfunction()

# Drops a slash at the end of the URL in <var>; stops when the URL is empty.
function(_argx_changelog_repo_url var what)
    set(url "${${var}}")
    string(REGEX REPLACE "/$" "" url "${url}")
    if(url STREQUAL "")
        message(FATAL_ERROR "error: ${what}: the repository URL is empty; \
pass https://github.com/<owner>/<repo>.")
    endif()
    set(${var} "${url}" PARENT_SCOPE)
endfunction()

function(argx_changelog_parse text prefix)
    _argx_changelog_scan("${text}" scan)
    foreach(field IN ITEMS VALID ERROR VERSIONS UNRELEASED_ENTRIES)
        set(${prefix}_${field} "${scan_${field}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(argx_changelog_promote text version date repo_url out_var)
    set(what "cannot move [Unreleased] to the version ${version}")
    _argx_changelog_repo_url(repo_url "${what}")
    argx_semver_parse("${version}" v)
    if(NOT v_VALID)
        message(FATAL_ERROR "error: ${what}: ${v_ERROR}")
    endif()
    if(NOT date MATCHES "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
        message(FATAL_ERROR "error: ${what}: the date \"${date}\" is not of the form YYYY-MM-DD.")
    endif()

    _argx_changelog_scan("${text}" scan)
    if(NOT scan_VALID)
        message(FATAL_ERROR "error: ${what}: the changelog is not valid: ${scan_ERROR}")
    endif()
    if(scan_UNRELEASED_ENTRIES EQUAL 0)
        message(FATAL_ERROR "error: ${what}: [Unreleased] has no entry; \
add the changes of the release under it first.")
    endif()
    set(previous "")
    if(NOT scan_VERSIONS STREQUAL "")
        if(version IN_LIST scan_VERSIONS)
            message(FATAL_ERROR "error: ${what}: the version ${version} is already listed.")
        endif()
        list(GET scan_VERSIONS 0 previous)
        argx_semver_compare("${version}" "${previous}" order)
        if(NOT order EQUAL 1)
            message(FATAL_ERROR "error: ${what}: the version ${version} is not higher than \
${previous}, the newest version in the changelog.")
        endif()
    endif()

    set(nl "${scan_NL}")
    set(lines "${scan_LINES}")
    set(unreleased ${scan_UNRELEASED_LINE})
    # The body of [Unreleased] ends before the newest version or the links.
    set(body_end ${scan_LINKS_LINE})
    if(NOT scan_VERSION_LINES STREQUAL "")
        list(GET scan_VERSION_LINES 0 body_end)
    endif()
    math(EXPR body_first "${unreleased} + 1")
    math(EXPR body_last "${body_end} - 1")
    # The body holds an entry, so it is not empty once trimmed.
    _argx_changelog_trim("${lines}" body_first body_last)

    set(unreleased_link "[Unreleased]: ${repo_url}/compare/v${version}...HEAD")
    if(previous STREQUAL "")
        set(version_link "[${version}]: ${repo_url}/releases/tag/v${version}")
    else()
        set(version_link "[${version}]: ${repo_url}/compare/v${previous}...v${version}")
    endif()

    set(result "")
    math(EXPR last "${scan_COUNT} - 1")
    foreach(index RANGE ${last})
        _argx_changelog_raw_line("${lines}" ${index} raw)
        # Every element but the last ended with "\n" in the text.
        set(ending "\n")
        if(index EQUAL last)
            set(ending "")
        endif()
        if(index EQUAL unreleased)
            string(APPEND result "${raw}${ending}${nl}## [${version}] - ${date}${nl}${nl}")
        elseif(index GREATER unreleased AND index LESS body_end)
            if(index GREATER_EQUAL body_first AND index LESS_EQUAL body_last)
                string(APPEND result "${raw}${ending}")
                if(index EQUAL body_last)
                    string(APPEND result "${nl}")
                endif()
            endif()
        elseif(index EQUAL scan_UNRELEASED_LINK_LINE)
            set(cr "")
            if(raw MATCHES "\r$")
                set(cr "\r")
            endif()
            string(APPEND result "${unreleased_link}${nl}${version_link}${cr}${ending}")
        else()
            string(APPEND result "${raw}${ending}")
        endif()
    endforeach()

    set(${out_var} "${result}" PARENT_SCOPE)
endfunction()

function(argx_changelog_notes text version repo_url out_var)
    set(what "cannot take the notes of the version ${version} out of the changelog")
    _argx_changelog_repo_url(repo_url "${what}")
    _argx_changelog_scan("${text}" scan)
    if(NOT scan_VALID)
        message(FATAL_ERROR "error: ${what}: the changelog is not valid: ${scan_ERROR}")
    endif()
    list(FIND scan_VERSIONS "${version}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "error: ${what}: the changelog has no section for the version \
${version}.")
    endif()

    set(nl "${scan_NL}")
    set(lines "${scan_LINES}")
    list(LENGTH scan_VERSIONS version_count)
    list(GET scan_VERSION_LINES ${position} heading)
    math(EXPR next "${position} + 1")
    set(body_end ${scan_LINKS_LINE})
    if(next LESS version_count)
        list(GET scan_VERSION_LINES ${next} body_end)
    endif()
    math(EXPR body_first "${heading} + 1")
    math(EXPR body_last "${body_end} - 1")
    _argx_changelog_trim("${lines}" body_first body_last)

    set(notes "")
    if(body_first LESS_EQUAL body_last)
        foreach(index RANGE ${body_first} ${body_last})
            _argx_changelog_line("${lines}" ${index} line)
            if(index GREATER body_first)
                string(APPEND notes "${nl}")
            endif()
            string(APPEND notes "${line}")
        endforeach()
    endif()

    # The pre-releases of the same core right below a release.
    argx_semver_parse("${version}" v)
    set(references "")
    if(v_PRERELEASE STREQUAL "")
        set(index ${next})
        while(index LESS version_count)
            list(GET scan_VERSIONS ${index} below)
            argx_semver_parse("${below}" b)
            if(b_PRERELEASE STREQUAL "" OR NOT b_CORE STREQUAL v_CORE)
                break()
            endif()
            if(NOT references STREQUAL "")
                string(APPEND references ", ")
            endif()
            string(APPEND references "[v${below}](${repo_url}/releases/tag/v${below})")
            math(EXPR index "${index} + 1")
        endwhile()
    endif()
    if(NOT references STREQUAL "")
        if(NOT notes STREQUAL "")
            string(APPEND notes "${nl}${nl}")
        endif()
        string(APPEND notes
            "This release also includes the changes in the pre-releases ${references}.")
    endif()

    set(${out_var} "${notes}" PARENT_SCOPE)
endfunction()

function(argx_changelog_check_version text version base prefix)
    set(what "cannot check the version ${version} against the changelog")
    argx_semver_parse("${version}" v)
    if(NOT v_VALID)
        message(FATAL_ERROR "error: ${what}: ${v_ERROR}")
    endif()
    if(NOT base STREQUAL "")
        argx_semver_parse("${base}" b)
        if(NOT b_VALID)
            message(FATAL_ERROR "error: ${what}: the earlier release: ${b_ERROR}")
        endif()
        if(NOT b_PRERELEASE STREQUAL "")
            message(FATAL_ERROR "error: ${what}: the earlier release ${base} is a pre-release.")
        endif()
    endif()
    _argx_changelog_scan("${text}" scan)
    if(NOT scan_VALID)
        message(FATAL_ERROR "error: ${what}: the changelog is not valid: ${scan_ERROR}")
    endif()

    # The kind of release, from the version that a pre-release leads to.
    if(base STREQUAL "")
        set(kind first)
    elseif(NOT v_MAJOR EQUAL b_MAJOR)
        set(kind major)
    elseif(NOT v_MINOR EQUAL b_MINOR)
        set(kind minor)
    else()
        set(kind patch)
    endif()

    set(problem "")
    set(breaking 0)
    set(numbers "${scan_ENTRY_NUMBERS}")
    set(entry_categories "${scan_ENTRY_CATEGORIES}")
    set(marks "${scan_ENTRY_MARKS}")
    list(LENGTH numbers count)

    # The form of the entries, whatever the version.
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE ${last})
            list(GET numbers ${index} number)
            list(GET entry_categories ${index} category)
            list(GET marks ${index} mark)
            if(mark STREQUAL "breaking")
                math(EXPR breaking "${breaking} + 1")
            endif()
            if(NOT problem STREQUAL "")
                continue()
            endif()
            if(category STREQUAL "-")
                set(problem "line ${number}: the entry is under no category heading. Put it \
under the category of the change: Added, Changed, Deprecated, Removed, Fixed or Security.")
            elseif(mark STREQUAL "malformed")
                set(problem "line ${number}: the entry starts with a marker close to \
\"**BREAKING**: \". Write the marker of a breaking change as \"**BREAKING**: \", in capitals \
with the colon after the asterisks.")
            elseif(mark STREQUAL "breaking" AND NOT category MATCHES "^(Changed|Removed)$")
                set(problem "line ${number}: the **BREAKING** entry is under ${category}. \
A breaking change goes under Changed or Removed.")
            endif()
        endforeach()
    endif()
    if(problem STREQUAL "" AND NOT scan_EMPTY_HEADINGS STREQUAL "")
        list(GET scan_EMPTY_HEADINGS 0 number)
        set(problem "line ${number}: the category heading has no entry under it. Remove the \
heading, or add the entries.")
    endif()

    # The version against the entries. The entry that needs the highest kind
    # of release, the first one of them, is named.
    if(problem STREQUAL "" AND NOT kind STREQUAL "first" AND count GREATER 0)
        set(ranks patch minor major)
        list(FIND ranks ${kind} have)
        set(need 0)
        set(need_index -1)
        foreach(index RANGE ${last})
            list(GET entry_categories ${index} category)
            list(GET marks ${index} mark)
            if(mark STREQUAL "breaking" AND b_MAJOR GREATER 0)
                set(rank 2)
            elseif(mark STREQUAL "breaking" OR NOT category MATCHES "^(Fixed|Security)$")
                set(rank 1)
            else()
                set(rank 0)
            endif()
            if(rank GREATER need)
                set(need ${rank})
                set(need_index ${index})
            endif()
        endforeach()
        if(need GREATER have)
            list(GET numbers ${need_index} number)
            list(GET entry_categories ${need_index} category)
            list(GET marks ${need_index} mark)
            if(need EQUAL 2)
                math(EXPR next_major "${b_MAJOR} + 1")
                set(lowest "${next_major}.0.0")
                set(reason "a **BREAKING** entry, which needs a major release from 1.0 on")
            else()
                math(EXPR next_minor "${b_MINOR} + 1")
                set(lowest "${b_MAJOR}.${next_minor}.0")
                if(mark STREQUAL "breaking")
                    set(reason "a **BREAKING** entry, which needs at least a minor release")
                else()
                    set(reason "an entry under ${category}, which needs at least a minor \
release; a patch release may have Fixed and Security entries only")
                endif()
            endif()
            set(problem "the version ${version} is a ${kind} release after ${base}, but \
line ${number} is ${reason}. Release ${lowest} or higher.")
        endif()
    endif()

    if(problem STREQUAL "")
        set(${prefix}_VALID TRUE PARENT_SCOPE)
    else()
        set(${prefix}_VALID FALSE PARENT_SCOPE)
    endif()
    set(${prefix}_ERROR "${problem}" PARENT_SCOPE)
    set(${prefix}_KIND ${kind} PARENT_SCOPE)
    set(${prefix}_BREAKING ${breaking} PARENT_SCOPE)
endfunction()

cmake_policy(POP)
