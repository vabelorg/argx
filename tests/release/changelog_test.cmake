# Copyright (c) 2026 The ArgX Authors
# SPDX-License-Identifier: MIT
#
# Tests of tools/release/ArgXChangelog.cmake: parsing a changelog and
# rejecting every form it does not accept, moving the body of [Unreleased] to
# the section of a new version with the link definitions updated, taking the
# notes of a version out of its section, and checking the entries under
# [Unreleased] against the version to release.
#
# Run by CTest as release.changelog; see ReleaseTestHelpers.cmake.

cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/ReleaseTestHelpers.cmake")
include("${ARGX_SOURCE_ROOT}/tools/release/ArgXChangelog.cmake")

set(repo "https://github.com/example/project")

# Sets <out-var> to the bytes of tests/release/fixtures/<fixture>.
function(read_fixture fixture out_var)
    argx_test_copy_fixture("${fixture}" path)
    file(READ "${path}" text)
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to <text> with every line ending turned into CRLF. The
# fixtures are checked out with LF, so the CRLF cases are made here.
function(to_crlf text out_var)
    string(REPLACE "\n" "\r\n" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_fixture(CHANGELOG-first-release.md first_text)
read_fixture(CHANGELOG-first-release-expected.md first_expected)
read_fixture(CHANGELOG-second-release.md second_text)
read_fixture(CHANGELOG-second-release-expected.md second_expected)
read_fixture(CHANGELOG-history.md history_text)
read_fixture(CHANGELOG-no-entries.md no_entries_text)
read_fixture(CHANGELOG-invalid.md invalid_text)

# --- Parsing valid changelogs ------------------------------------------------

# Parses <text> and checks that it is valid with <versions> and <entries>.
function(expect_parse_valid text what versions entries)
    foreach(field IN ITEMS VALID ERROR VERSIONS UNRELEASED_ENTRIES)
        set(c_${field} "stale")
    endforeach()
    argx_changelog_parse("${text}" c)
    argx_test_expect_equal("${c_VALID}" "TRUE" "${what}: valid")
    argx_test_expect_equal("${c_ERROR}" "" "${what}: no error")
    argx_test_expect_equal("${c_VERSIONS}" "${versions}" "${what}: the versions")
    argx_test_expect_equal("${c_UNRELEASED_ENTRIES}" "${entries}"
        "${what}: the entries under [Unreleased]")
endfunction()

set(history_versions 1.0.0 0.3.0-rc.1 0.2.0 0.2.0-rc.2 0.2.0-rc.1 0.1.0)

# No version yet, as before the first release. A nested item and a
# continuation line are not entries; "* " starts an entry as "- " does.
expect_parse_valid("${first_text}" "first release" "" 3)
expect_parse_valid("${first_expected}" "after the first release" "0.1.0" 0)
expect_parse_valid("${second_text}" "second release" "0.1.0" 2)
expect_parse_valid("${second_expected}" "after the second release" "0.2.0-rc.1;0.1.0" 0)
expect_parse_valid("${history_text}" "history" "${history_versions}" 1)
# A category heading without entries is not an entry.
expect_parse_valid("${no_entries_text}" "no entries" "0.1.0" 0)

# CRLF line endings are accepted.
to_crlf("${history_text}" history_crlf)
expect_parse_valid("${history_crlf}" "history with CRLF" "${history_versions}" 1)
to_crlf("${first_text}" first_crlf)
expect_parse_valid("${first_crlf}" "first release with CRLF" "" 3)

# The last line may lack its line ending.
string(REGEX REPLACE "\n$" "" history_no_final_newline "${history_text}")
expect_parse_valid("${history_no_final_newline}" "history without a final newline"
    "${history_versions}" 1)

# Every category of Keep a Changelog is accepted.
set(all_categories "# Changelog\n\n## [Unreleased]\n\n")
foreach(category IN ITEMS Added Changed Deprecated Removed Fixed Security)
    string(APPEND all_categories "### ${category}\n\n- An entry.\n\n")
endforeach()
string(APPEND all_categories "[Unreleased]: ${repo}/commits/main\n")
expect_parse_valid("${all_categories}" "every category" "" 6)

# --- Parsing invalid changelogs ----------------------------------------------

# Parses <text> and checks that it is invalid with a reason that holds <part>.
function(expect_parse_invalid text what part)
    foreach(field IN ITEMS VALID ERROR VERSIONS UNRELEASED_ENTRIES)
        set(c_${field} "stale")
    endforeach()
    argx_changelog_parse("${text}" c)
    argx_test_expect_equal("${c_VALID}" "FALSE" "${what}: invalid")
    argx_test_expect_contains("${c_ERROR}" "${part}" "${what}: the reason")
    argx_test_expect_equal("${c_VERSIONS}" "" "${what}: no versions")
    argx_test_expect_equal("${c_UNRELEASED_ENTRIES}" "" "${what}: no entry count")
endfunction()

# Sets <out-var> to the history fixture with <old> replaced by <new>, after
# checking that the fixture holds <old>.
function(edit_history old new out_var)
    argx_test_expect_contains("${history_text}" "${old}" "the history fixture holds [${old}]")
    string(REPLACE "${old}" "${new}" text "${history_text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

expect_parse_invalid("" "an empty text" "line 1: the first line must be \"# Changelog\"")
expect_parse_invalid("Changelog\n" "no title" "line 1: the first line must be \"# Changelog\"")
expect_parse_invalid("\n# Changelog\n" "a blank line before the title"
    "line 1: the first line must be \"# Changelog\"")
expect_parse_invalid("# Changelog\n\nText.\n" "no [Unreleased] section"
    "there is no \"## [Unreleased]\" heading")

expect_parse_invalid("${invalid_text}" "the invalid fixture"
    "line 5: \"### Improved\" is not a category of Keep a Changelog")

edit_history("## [Unreleased]\n" "" text)
expect_parse_invalid("${text}" "no [Unreleased] heading"
    "line 10: the first \"## \" heading must be \"## [Unreleased]\"")

edit_history("## [0.1.0] - 2026-11-13\n" "## [0.1.0] - 2026-11-13\n\n## [Unreleased]\n" text)
expect_parse_invalid("${text}" "[Unreleased] twice" "\"## [Unreleased]\" appears more than once")

edit_history("## [0.2.0] - 2027-01-15" "## [0.2.0]" text)
expect_parse_invalid("${text}" "a version without a date"
    "line 25: the heading \"## [0.2.0]\" is not of the form \"## [<version>] - <YYYY-MM-DD>\"")

edit_history("## [0.2.0] - 2027-01-15" "## [0.2.0] - 2027-1-15" text)
expect_parse_invalid("${text}" "a date of the wrong form"
    "line 25: the heading \"## [0.2.0] - 2027-1-15\" is not of the form")

edit_history("## [0.2.0] - 2027-01-15" "## 0.2.0 - 2027-01-15" text)
expect_parse_invalid("${text}" "a version without brackets" "line 25: the heading \"## 0.2.0 - 2027-01-15\"")

edit_history("## [0.2.0] - 2027-01-15" "## [v0.2.0] - 2027-01-15" text)
expect_parse_invalid("${text}" "a version with a \"v\""
    "line 25: \"v0.2.0\" is not a valid version")

edit_history("## [0.2.0] - 2027-01-15" "## [0.2] - 2027-01-15" text)
expect_parse_invalid("${text}" "an incomplete version" "line 25: \"0.2\" is not a valid version")

edit_history("## [0.2.0] - 2027-01-15" "## [0.2.0+build] - 2027-01-15" text)
expect_parse_invalid("${text}" "a version with build metadata" "build metadata")

edit_history("## [0.2.0-rc.2] - 2027-01-10" "## [0.2.0] - 2027-01-10" text)
expect_parse_invalid("${text}" "a version listed twice" "line 31: the version 0.2.0 is listed twice")

edit_history("## [0.2.0-rc.2] - 2027-01-10" "## [0.4.0] - 2027-01-10" text)
expect_parse_invalid("${text}" "versions out of order"
    "line 31: the version 0.4.0 is not lower than 0.2.0, the version above it")

# A pre-release is lower than the release of the same core, so it must come
# below it.
edit_history("## [0.2.0] - 2027-01-15" "## [0.2.0-rc.3] - 2027-01-15" text)
string(REPLACE "## [0.2.0-rc.1] - 2027-01-05" "## [0.2.0] - 2027-01-05" text "${text}")
expect_parse_invalid("${text}" "a release below its pre-release"
    "line 37: the version 0.2.0 is not lower than 0.2.0-rc.2, the version above it")

edit_history("### Fixed\n\n- A fix in 0.2.0.\n" "### Fixes\n\n- A fix in 0.2.0.\n" text)
expect_parse_invalid("${text}" "an unknown category"
    "line 27: \"### Fixes\" is not a category of Keep a Changelog")

edit_history("### Fixed\n\n- A fix in 0.2.0.\n" "### fixed\n\n- A fix in 0.2.0.\n" text)
expect_parse_invalid("${text}" "a category in lower case" "line 27: \"### fixed\" is not a category")

edit_history("## [0.1.0] - 2026-11-13\n" "## Notes\n\n## [0.1.0] - 2026-11-13\n" text)
expect_parse_invalid("${text}" "another \"## \" heading"
    "line 43: the heading \"## Notes\" is not of the form")

edit_history("[Unreleased]: ${repo}/compare/v1.0.0...HEAD\n" "" text)
expect_parse_invalid("${text}" "no [Unreleased] link"
    "there must be exactly one \"[Unreleased]: \" link definition; there are 0")

edit_history("[0.1.0]: " "[Unreleased]: ${repo}/commits/main\n[0.1.0]: " text)
expect_parse_invalid("${text}" "two [Unreleased] links"
    "there must be exactly one \"[Unreleased]: \" link definition; there are 2")

edit_history("[0.1.0]: " "## [0.0.1] - 2026-01-01\n[0.1.0]: " text)
expect_parse_invalid("${text}" "a heading among the link definitions"
    "line 55: only link definitions and blank lines may follow the first link definition")

set(text "${history_text}Some text at the end.\n")
expect_parse_invalid("${text}" "text after the link definitions"
    "only link definitions and blank lines may follow the first link definition")

edit_history("A preamble line.\n" "[a]: https://example.com\n" text)
expect_parse_invalid("${text}" "a link definition before [Unreleased]"
    "line 3: a link definition comes before \"## [Unreleased]\"")

# The reasons of a CRLF text are the same, without a carriage return.
to_crlf("${invalid_text}" invalid_crlf)
expect_parse_invalid("${invalid_crlf}" "the invalid fixture with CRLF"
    "line 5: \"### Improved\" is not a category of Keep a Changelog")
argx_changelog_parse("${invalid_crlf}" c)
argx_test_expect_not_contains("${c_ERROR}" "\r" "the invalid fixture with CRLF: no carriage return")

# --- Promoting [Unreleased] --------------------------------------------------

# The first version links to its tag; [Unreleased] compares from it.
argx_changelog_promote("${first_text}" "0.1.0" "2026-11-13" "${repo}" result)
argx_test_expect_equal("${result}" "${first_expected}" "promote the first release: the whole text")

# The second version compares from the version right below it.
argx_changelog_promote("${second_text}" "0.2.0-rc.1" "2026-12-01" "${repo}" result)
argx_test_expect_equal("${result}" "${second_expected}" "promote the second release: the whole text")

# A slash at the end of the repository URL is not doubled.
argx_changelog_promote("${second_text}" "0.2.0-rc.1" "2026-12-01" "${repo}/" result)
argx_test_expect_equal("${result}" "${second_expected}" "promote with a slash after the URL")

# A third version is linked to the newest version of the file.
argx_changelog_promote("${history_text}" "1.0.1" "2027-04-01" "${repo}" result)
expect_parse_valid("${result}" "after promoting 1.0.1" "1.0.1;${history_versions}" 0)
argx_test_expect_contains("${result}"
    "## [Unreleased]\n\n## [1.0.1] - 2027-04-01\n\n### Added\n\n- An entry that is not released yet.\n\n## [1.0.0] - 2027-03-01\n"
    "promote 1.0.1: the sections")
argx_test_expect_contains("${result}"
    "\n[Unreleased]: ${repo}/compare/v1.0.1...HEAD\n[1.0.1]: ${repo}/compare/v1.0.0...v1.0.1\n[1.0.0]: "
    "promote 1.0.1: the links")

# CRLF stays CRLF, with no LF alone, in the lines kept and the lines added.
to_crlf("${first_expected}" first_expected_crlf)
argx_changelog_promote("${first_crlf}" "0.1.0" "2026-11-13" "${repo}" result)
argx_test_expect_equal("${result}" "${first_expected_crlf}" "promote with CRLF: the whole text")
to_crlf("${second_text}" second_crlf)
to_crlf("${second_expected}" second_expected_crlf)
argx_changelog_promote("${second_crlf}" "0.2.0-rc.1" "2026-12-01" "${repo}" result)
argx_test_expect_equal("${result}" "${second_expected_crlf}" "promote the second release with CRLF")
string(REPLACE "\r\n" "" lone "${result}")
argx_test_expect_not_contains("${lone}" "\n" "promote with CRLF: no LF alone")

# Without a final line ending, none is added.
string(REGEX REPLACE "\n$" "" text "${second_text}")
string(REGEX REPLACE "\n$" "" expected "${second_expected}")
argx_changelog_promote("${text}" "0.2.0-rc.1" "2026-12-01" "${repo}" result)
argx_test_expect_equal("${result}" "${expected}" "promote without a final newline")

# --- Taking the notes out ----------------------------------------------------

# Checks the notes of <version> in <text>.
function(expect_notes text version expected)
    set(notes "stale")
    argx_changelog_notes("${text}" "${version}" "${repo}" notes)
    argx_test_expect_equal("${notes}" "${expected}" "notes of ${version}")
endfunction()

# The body runs to the next heading, without the blank lines around it.
expect_notes("${history_text}" "1.0.0"
    "### Changed\n\n- The first stable release.\n  - With a nested item.")
# A pre-release gets no reference.
expect_notes("${history_text}" "0.3.0-rc.1" "### Added\n\n- A pre-release of 0.3.0.")
expect_notes("${history_text}" "0.2.0-rc.2" "### Fixed\n\n- A fix in 0.2.0-rc.2.")
# A release refers to the pre-releases of the same core right below it, in the
# order of the file.
set(notes_020 "### Fixed\n\n- A fix in 0.2.0.\n\nThis release also includes the changes in the pre-releases \
[v0.2.0-rc.2](${repo}/releases/tag/v0.2.0-rc.2), \
[v0.2.0-rc.1](${repo}/releases/tag/v0.2.0-rc.1).")
expect_notes("${history_text}" "0.2.0" "${notes_020}")
# The last section runs to the link definitions.
expect_notes("${history_text}" "0.1.0" "### Added\n\n- The first release.")

# Only the pre-releases right below count: a pre-release of the same core
# further down, after another version, is not referred to.
edit_history("## [0.2.0-rc.1] - 2027-01-05" "## [0.1.1] - 2027-01-05" text)
string(REPLACE "## [0.1.0] - 2026-11-13" "## [0.1.1-rc.1] - 2026-11-13" text "${text}")
expect_notes("${text}" "0.2.0" "### Fixed\n\n- A fix in 0.2.0.\n\nThis release also includes the changes in the pre-releases \
[v0.2.0-rc.2](${repo}/releases/tag/v0.2.0-rc.2).")
expect_notes("${text}" "0.1.1" "### Added\n\n- A feature in 0.2.0-rc.1.\n\nThis release also includes the changes in the pre-releases \
[v0.1.1-rc.1](${repo}/releases/tag/v0.1.1-rc.1).")

# The notes of a version just promoted.
expect_notes("${second_expected}" "0.2.0-rc.1"
    "### Changed\n\n- The help text wraps at 80 columns.\n\n### Security\n\n- Option values are checked for length.")

# The notes of a CRLF text keep CRLF.
to_crlf("${notes_020}" notes_020_crlf)
expect_notes("${history_crlf}" "0.2.0" "${notes_020_crlf}")

# --- Checking the version against the entries --------------------------------

# Sets <out-var> to a changelog with <body> under [Unreleased] and a released
# 0.1.0. The body starts on line 5.
function(entries_text body out_var)
    set(${out_var} "# Changelog\n\n## [Unreleased]\n\n${body}\n## [0.1.0] - 2026-11-13\n\n\
### Added\n\n- The first release.\n\n[Unreleased]: ${repo}/compare/v0.1.0...HEAD\n\
[0.1.0]: ${repo}/releases/tag/v0.1.0\n" PARENT_SCOPE)
endfunction()

# Checks <body> for <version> after <base>, and that the result is <valid>
# with <kind>, <breaking> breaking entries and an error that holds <part>
# ("" for no error).
function(expect_check what body version base valid kind breaking part)
    entries_text("${body}" text)
    foreach(field IN ITEMS VALID ERROR KIND BREAKING)
        set(k_${field} "stale")
    endforeach()
    argx_changelog_check_version("${text}" "${version}" "${base}" k)
    argx_test_expect_equal("${k_VALID}" "${valid}" "${what}: valid")
    argx_test_expect_equal("${k_KIND}" "${kind}" "${what}: the kind")
    argx_test_expect_equal("${k_BREAKING}" "${breaking}" "${what}: the breaking entries")
    if(part STREQUAL "")
        argx_test_expect_equal("${k_ERROR}" "" "${what}: no error")
    else()
        argx_test_expect_contains("${k_ERROR}" "${part}" "${what}: the reason")
    endif()
endfunction()

set(fixed "### Fixed\n\n- A fix.\n")
set(added "### Added\n\n- A function.\n")
set(breaking "### Changed\n\n- **BREAKING**: `argx_foo()` is renamed `argx_bar()`.\n")

# The kind of release, a pre-release checked as the version it leads to.
expect_check("first" "${added}" 0.1.0 "" TRUE first 0 "")
expect_check("patch" "${fixed}" 0.1.1 0.1.0 TRUE patch 0 "")
expect_check("minor" "${added}" 0.2.0 0.1.0 TRUE minor 0 "")
expect_check("major" "${breaking}" 1.0.0 0.9.3 TRUE major 1 "")
expect_check("a pre-release of a patch" "${fixed}" 0.1.1-rc.1 0.1.0 TRUE patch 0 "")
expect_check("a pre-release of a minor" "${added}" 0.2.0-rc.1 0.1.0 TRUE minor 0 "")

# Entries that a patch release may have, in either list form, with nested
# items and continuation lines, which are not entries.
expect_check("a patch with Fixed and Security"
    "### Fixed\n\n* A fix\n  over two lines.\n  - A nested item.\n\n### Security\n\n- A fix.\n"
    0.1.1 0.1.0 TRUE patch 0 "")
# A nested item that looks like a marker is not an entry.
expect_check("a nested **BREAKING** item"
    "### Fixed\n\n- A fix.\n  - **BREAKING**: not an entry.\n" 0.1.1 0.1.0 TRUE patch 0 "")
# "Breaking" without emphasis or a colon is a word of the entry.
expect_check("an entry that starts with Breaking"
    "### Fixed\n\n- Breaking up long lines no longer drops a character.\n"
    0.1.1 0.1.0 TRUE patch 0 "")

# What a patch release may not have.
foreach(category IN ITEMS Added Changed Deprecated Removed)
    expect_check("a patch with ${category}" "${fixed}\n### ${category}\n\n- A change.\n"
        0.1.1 0.1.0 FALSE patch 0
        "the version 0.1.1 is a patch release after 0.1.0, but line 11 is an entry under \
${category}, which needs at least a minor release; a patch release may have Fixed and Security \
entries only. Release 0.2.0 or higher.")
endforeach()
expect_check("a patch with a breaking change" "${breaking}" 0.1.1 0.1.0 FALSE patch 1
    "line 7 is a **BREAKING** entry, which needs at least a minor release. Release 0.2.0 or higher.")
expect_check("a pre-release of a patch with Added" "${added}" 1.2.1-rc.1 1.2.0 FALSE patch 0
    "the version 1.2.1-rc.1 is a patch release after 1.2.0")

# A breaking change needs a minor release before 1.0, and a major one after.
expect_check("a breaking change before 1.0" "${breaking}" 0.2.0 0.1.0 TRUE minor 1 "")
expect_check("a breaking change in a minor after 1.0" "${breaking}" 1.3.0 1.2.0 FALSE minor 1
    "the version 1.3.0 is a minor release after 1.2.0, but line 7 is a **BREAKING** entry, \
which needs a major release from 1.0 on. Release 2.0.0 or higher.")
expect_check("a breaking change in a major after 1.0" "${breaking}" 2.0.0 1.2.0 TRUE major 1 "")
expect_check("an addition in a minor after 1.0" "${added}" 1.3.0 1.2.0 TRUE minor 0 "")
# The entry named is the first one that needs the highest kind of release.
expect_check("the entry that needs the most"
    "### Added\n\n- A function.\n\n### Removed\n\n- A macro.\n- **BREAKING**: `argx_foo()` is \
removed.\n- **BREAKING**: `argx_bar()` is removed.\n" 1.3.0 1.2.0 FALSE minor 2
    "line 12 is a **BREAKING** entry")

# The form of the entries, checked for the first release too.
expect_check("an entry under no category" "- A change.\n\n${fixed}" 0.2.0 0.1.0 FALSE minor 0
    "line 5: the entry is under no category heading.")
expect_check("an entry under no category, first release" "- A change.\n" 0.1.0 "" FALSE first 0
    "line 5: the entry is under no category heading.")
foreach(marker IN ITEMS "**Breaking**:" "**BREAKING:**" "**BREAKING** " "BREAKING:"
        "breaking:" "__BREAKING__:" "*BREAKING*:")
    expect_check("the marker [${marker}]" "### Changed\n\n- ${marker} A change.\n"
        0.2.0 0.1.0 FALSE minor 0
        "line 7: the entry starts with a marker close to \"**BREAKING**: \".")
endforeach()
foreach(category IN ITEMS Added Deprecated Fixed Security)
    expect_check("a breaking change under ${category}"
        "### ${category}\n\n- **BREAKING**: A change.\n" 1.0.0 0.9.0 FALSE major 1
        "line 7: the **BREAKING** entry is under ${category}. A breaking change goes under \
Changed or Removed.")
endforeach()
expect_check("a breaking change under Removed" "### Removed\n\n- **BREAKING**: A macro.\n"
    0.2.0 0.1.0 TRUE minor 1 "")
expect_check("an empty category before another" "### Added\n\n${fixed}" 0.1.1 0.1.0 FALSE patch 0
    "line 5: the category heading has no entry under it.")
expect_check("an empty category at the end" "${fixed}\n### Security\n" 0.1.1 0.1.0 FALSE patch 0
    "line 9: the category heading has no entry under it.")
# A category without entries in a released section is not checked.
expect_parse_valid("${no_entries_text}" "no entries, again" "0.1.0" 0)
argx_changelog_check_version("${history_text}" 1.1.0 1.0.0 k)
argx_test_expect_equal("${k_VALID}" "TRUE" "the history: valid for 1.1.0")

# The same with CRLF line endings, and without a final line ending.
entries_text("${breaking}" text)
to_crlf("${text}" text)
argx_changelog_check_version("${text}" 1.3.0 1.2.0 k)
argx_test_expect_contains("${k_ERROR}" "line 7 is a **BREAKING** entry" "CRLF: the reason")
argx_test_expect_not_contains("${k_ERROR}" "\r" "CRLF: no carriage return")
set(text "# Changelog\n\n## [Unreleased]\n\n### Fixed\n\n- A fix.\n\n### Added\n\n[Unreleased]: x")
argx_changelog_check_version("${text}" 0.1.1 0.1.0 k)
argx_test_expect_contains("${k_ERROR}" "line 9: the category heading has no entry"
    "an empty category at the end of the text")

# --- Failures of promote and notes -------------------------------------------
#
# Each case runs the function in a child process, since a FATAL_ERROR would
# stop this script. The text is passed in a file.

set(driver "${ARGX_TEST_WORK_DIR}/driver.cmake")
file(WRITE "${driver}" "
include(\"${ARGX_SOURCE_ROOT}/tools/release/ArgXChangelog.cmake\")
file(READ \"\${TEXT_FILE}\" text)
if(ACTION STREQUAL \"promote\")
    argx_changelog_promote(\"\${text}\" \"\${VERSION}\" \"\${DATE}\" \"\${REPO}\" result)
elseif(ACTION STREQUAL \"check\")
    argx_changelog_check_version(\"\${text}\" \"\${VERSION}\" \"\${BASE}\" k)
    set(result \"\${k_VALID}\")
else()
    argx_changelog_notes(\"\${text}\" \"\${VERSION}\" \"\${REPO}\" result)
endif()
message(\"done: [\${result}]\")
")

set(case_number 0)

# Runs <action> on <text> with the -D arguments that follow, and checks that it
# stops with an error that holds <part>.
function(expect_rejected what text part)
    math(EXPR number "${case_number} + 1")
    set(case_number ${number} PARENT_SCOPE)
    set(text_file "${ARGX_TEST_WORK_DIR}/case-${number}.md")
    file(WRITE "${text_file}" "${text}")
    argx_test_run_script(run "${driver}" -D "TEXT_FILE=${text_file}" ${ARGN})
    set(outcome "failed")
    if(run_RESULT EQUAL 0)
        set(outcome "succeeded")
    endif()
    argx_test_expect_equal("${outcome}" "failed" "${what}: the outcome")
    argx_test_expect_not_contains("${run_OUTPUT}" "done: [" "${what}: no result")
    argx_test_expect_contains("${run_OUTPUT}" "error: " "${what}: an error")
    # CMake wraps the text of an error into lines; join them again.
    string(REGEX REPLACE "[ \n]+" " " flat "${run_OUTPUT}")
    argx_test_expect_contains("${flat}" "${part}" "${what}: the reason")
endfunction()

set(promote -D ACTION=promote -D DATE=2027-04-01 -D "REPO=${repo}")
expect_rejected("promote an invalid changelog" "${invalid_text}"
    "the changelog is not valid: line 5:" ${promote} -D VERSION=0.1.0)
expect_rejected("promote without entries" "${no_entries_text}"
    "[Unreleased] has no entry" ${promote} -D VERSION=0.2.0)
expect_rejected("promote a version already listed" "${history_text}"
    "the version 0.2.0 is already listed" ${promote} -D VERSION=0.2.0)
expect_rejected("promote a version lower than the newest" "${history_text}"
    "the version 0.4.0 is not higher than 1.0.0, the newest version" ${promote} -D VERSION=0.4.0)
expect_rejected("promote a pre-release of the newest version" "${history_text}"
    "the version 1.0.0-rc.1 is not higher than 1.0.0" ${promote} -D VERSION=1.0.0-rc.1)
expect_rejected("promote an invalid version" "${first_text}"
    "\"0.1\" is not a valid version" ${promote} -D VERSION=0.1)
expect_rejected("promote with an invalid date" "${first_text}"
    "the date \"2026-11-1\" is not of the form YYYY-MM-DD"
    -D ACTION=promote -D DATE=2026-11-1 -D "REPO=${repo}" -D VERSION=0.1.0)
expect_rejected("promote without a repository URL" "${first_text}"
    "the repository URL is empty" -D ACTION=promote -D DATE=2026-11-13 -D VERSION=0.1.0)

set(notes -D ACTION=notes -D "REPO=${repo}")
expect_rejected("notes of a missing version" "${history_text}"
    "the changelog has no section for the version 0.4.0" ${notes} -D VERSION=0.4.0)
expect_rejected("notes of [Unreleased]" "${history_text}"
    "the changelog has no section for the version Unreleased" ${notes} -D VERSION=Unreleased)
expect_rejected("notes of an invalid changelog" "${invalid_text}"
    "the changelog is not valid: line 5:" ${notes} -D VERSION=0.1.0)
expect_rejected("notes without a repository URL" "${history_text}"
    "the repository URL is empty" -D ACTION=notes -D VERSION=0.2.0)

set(check -D ACTION=check)
expect_rejected("check an invalid changelog" "${invalid_text}"
    "the changelog is not valid: line 5:" ${check} -D VERSION=0.2.0 -D BASE=0.1.0)
expect_rejected("check an invalid version" "${first_text}"
    "\"0.2\" is not a valid version" ${check} -D VERSION=0.2 -D BASE=0.1.0)
expect_rejected("check against an invalid base" "${first_text}"
    "the earlier release: \"v0.1.0\" is not a valid version" ${check} -D VERSION=0.2.0
    -D BASE=v0.1.0)
expect_rejected("check against a pre-release" "${first_text}"
    "the earlier release 0.2.0-rc.1 is a pre-release" ${check} -D VERSION=0.2.0
    -D BASE=0.2.0-rc.1)

# The driver itself works, so the failures above come from the cases.
set(text_file "${ARGX_TEST_WORK_DIR}/driver-check.md")
file(WRITE "${text_file}" "${first_text}")
argx_test_run_script(run "${driver}" -D "TEXT_FILE=${text_file}" ${promote} -D VERSION=0.1.0)
argx_test_expect_equal("${run_RESULT}" "0" "promote in a child: the exit code")
argx_test_expect_contains("${run_OUTPUT}" "done: [# Changelog" "promote in a child: the result")
file(WRITE "${text_file}" "${history_text}")
argx_test_run_script(run "${driver}" -D "TEXT_FILE=${text_file}" ${notes} -D VERSION=0.2.0)
argx_test_expect_equal("${run_RESULT}" "0" "notes in a child: the exit code")
argx_test_expect_contains("${run_OUTPUT}" "done: [### Fixed" "notes in a child: the result")
file(WRITE "${text_file}" "${first_text}")
argx_test_run_script(run "${driver}" -D "TEXT_FILE=${text_file}" ${check} -D VERSION=0.2.0
    -D BASE=0.1.0)
argx_test_expect_equal("${run_RESULT}" "0" "check in a child: the exit code")
argx_test_expect_contains("${run_OUTPUT}" "done: [TRUE]" "check in a child: the result")

argx_test_finish()
