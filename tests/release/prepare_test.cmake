# Copyright (c) 2026 The ArgX Authors
# SPDX-License-Identifier: MIT
#
# Tests of tools/release/prepare.cmake: the script that checks a release before
# its pull request is opened and rewrites the version header and the changelog.
#
# Each case builds a small copy of a source tree in the work directory (the
# version header, CHANGELOG.md and one more file) and runs the script on it in
# a child process, as the release workflow runs it, so that its exit code, its
# output, the files it rewrites and the output file it appends to are checked
# as they are.
#
#   - The first release (the header holds 0.1.0, there is no tag, the version
#     is 0.1.0) and a pre-release after it succeed; only the header and the
#     changelog change, byte for byte as expected, with LF and with CRLF line
#     endings, and the output file gets version, tag and prerelease.
#   - Only tags of the form v<version> count as releases.
#   - With ARGX_RELEASE_CHECK_ONLY, nothing is written.
#   - Every check fails with a non-zero exit code and the reason, before any
#     file is written.
#
# Run by CTest as release.prepare; see ReleaseTestHelpers.cmake.

cmake_minimum_required(VERSION 3.21)

include("${CMAKE_CURRENT_LIST_DIR}/ReleaseTestHelpers.cmake")

set(script "${ARGX_SOURCE_ROOT}/tools/release/prepare.cmake")
set(repo_url "https://github.com/example/project")

argx_test_copy_fixture(version.h header_fixture fixture-version.h)
file(READ "${header_fixture}" header_fixture_text)

# Sets <out-var> to the text of the header fixture, which holds 1.2.3, with
# the four values replaced by those of <major>.<minor>.<patch>[-<prerelease>].
function(header_text major minor patch prerelease out_var)
    set(text "${header_fixture_text}")
    string(REPLACE "#define ARGX_VERSION_MAJOR 1\n"
        "#define ARGX_VERSION_MAJOR ${major}\n" text "${text}")
    string(REPLACE "#define ARGX_VERSION_MINOR 2\n"
        "#define ARGX_VERSION_MINOR ${minor}\n" text "${text}")
    string(REPLACE "#define ARGX_VERSION_PATCH 3\n"
        "#define ARGX_VERSION_PATCH ${patch}\n" text "${text}")
    string(REPLACE "#define ARGX_VERSION_PRERELEASE \"\"\n"
        "#define ARGX_VERSION_PRERELEASE \"${prerelease}\"\n" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to the text of the fixture <fixture>, read as it is.
function(fixture_text fixture out_var)
    file(READ "${ARGX_TEST_FIXTURE_DIR}/${fixture}" text)
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to <text> with every LF turned into CRLF.
function(to_crlf text out_var)
    string(REPLACE "\n" "\r\n" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to the bytes of <text> in hexadecimal, as file(READ HEX)
# gives them, so that two texts can be compared byte for byte.
function(text_hex text out_var)
    string(HEX "${text}" hex)
    set(${out_var} "${hex}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to the bytes of the file <path> in hexadecimal, or to
# "(no file)" when there is no such file.
function(file_hex path out_var)
    if(EXISTS "${path}")
        file(READ "${path}" hex HEX)
    else()
        set(hex "(no file)")
    endif()
    set(${out_var} "${hex}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to every file under <dir>, each with its bytes, so that a
# change of any file, and a file added or removed, changes the result.
function(tree_snapshot dir out_var)
    file(GLOB_RECURSE files RELATIVE "${dir}" "${dir}/*")
    list(SORT files)
    set(snapshot "")
    foreach(file IN LISTS files)
        file(READ "${dir}/${file}" hex HEX)
        string(APPEND snapshot "${file}=${hex}\n")
    endforeach()
    set(${out_var} "${snapshot}" PARENT_SCOPE)
endfunction()

# Makes the source tree <name> in the work directory, with <header> as
# include/argx/version.h, <changelog> as CHANGELOG.md and a README.md that
# the script must not touch. Sets <out-var> to the root of the tree.
function(make_tree name header changelog out_var)
    set(root "${ARGX_TEST_WORK_DIR}/${name}")
    argx_write_bytes("${root}/include/argx/version.h" "${header}" "the version header")
    if(changelog MATCHES "\n$")
        argx_write_bytes("${root}/CHANGELOG.md" "${changelog}" "the changelog")
    else()
        # Only a changelog without a line ending at its end, which cannot be
        # written byte for byte on Windows; the case does not need its bytes.
        file(WRITE "${root}/CHANGELOG.md" "${changelog}")
    endif()
    argx_write_bytes("${root}/README.md" "# A file that a release does not change.\n" "README.md")
    set(${out_var} "${root}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to <text> with every run of blanks and line endings made one
# blank, since CMake wraps the text of a FATAL_ERROR over several lines.
function(flatten text out_var)
    string(REGEX REPLACE "[ \t\r\n]+" " " text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Runs the script on the tree <root> with the version <version>, the date
# <date> and a tags file with the content <tags>, appending to the output file
# <root>.output, which starts as "earlier=value\n". Further arguments are
# passed to cmake before the script. Sets <prefix>_RESULT, <prefix>_OUTPUT
# (flattened) and <prefix>_FILE (the content of the output file).
function(run_prepare prefix root version date tags)
    set(tags_path "${root}.tags")
    set(output_path "${root}.output")
    file(WRITE "${tags_path}" "${tags}")
    file(WRITE "${output_path}" "earlier=value\n")
    argx_test_run_script(run "${script}"
        "-DARGX_RELEASE_VERSION=${version}"
        "-DARGX_RELEASE_DATE=${date}"
        "-DARGX_RELEASE_REPO_URL=${repo_url}"
        "-DARGX_SOURCE_DIR=${root}"
        "-DARGX_RELEASE_TAGS_FILE=${tags_path}"
        "-DARGX_RELEASE_OUTPUT=${output_path}"
        ${ARGN})
    flatten("${run_OUTPUT}" output)
    file(READ "${output_path}" output_file)
    set(${prefix}_RESULT "${run_RESULT}" PARENT_SCOPE)
    set(${prefix}_OUTPUT "${output}" PARENT_SCOPE)
    set(${prefix}_FILE "${output_file}" PARENT_SCOPE)
endfunction()

# Checks that a run succeeded and that the tree <root> now holds exactly
# <header> and <changelog>, besides the untouched README.md.
function(expect_release what prefix root header changelog)
    argx_test_expect_equal("${${prefix}_RESULT}" "0" "${what}: the script succeeds")
    if(NOT "${${prefix}_RESULT}" STREQUAL "0")
        message("${what}: output of the script:\n${${prefix}_OUTPUT}")
    endif()
    text_hex("${header}" header_hex)
    text_hex("${changelog}" changelog_hex)
    text_hex("# A file that a release does not change.\n" readme_hex)
    tree_snapshot("${root}" actual)
    argx_test_expect_equal("${actual}"
        "CHANGELOG.md=${changelog_hex}\nREADME.md=${readme_hex}\ninclude/argx/version.h=${header_hex}\n"
        "${what}: only the header and the changelog change, as expected")
endfunction()

# --- The first release ---------------------------------------------------------

# The header already holds 0.1.0 and there is no tag: 0.1.0 itself is
# accepted, and the header stays as it is.
header_text(0 1 0 "" header_0_1_0)
fixture_text(CHANGELOG-first-release.md first_changelog)
fixture_text(CHANGELOG-first-release-expected.md first_expected)

make_tree(first "${header_0_1_0}" "${first_changelog}" root)
run_prepare(run "${root}" 0.1.0 2026-11-13 "")
expect_release("the first release" run "${root}" "${header_0_1_0}" "${first_expected}")
argx_test_expect_equal("${run_FILE}"
    "earlier=value\nversion=0.1.0\ntag=v0.1.0\nprerelease=false\nbreaking=false\n"
    "the first release: the output file")
argx_test_expect_contains("${run_OUTPUT}" "0.1.0"
    "the first release: the standard output names the version")
argx_test_expect_contains("${run_OUTPUT}" "CHANGELOG.md"
    "the first release: the standard output names the changelog")

# The same with CRLF line endings: they are kept in both files.
to_crlf("${header_0_1_0}" header_0_1_0_crlf)
to_crlf("${first_changelog}" first_changelog_crlf)
to_crlf("${first_expected}" first_expected_crlf)
make_tree(first_crlf "${header_0_1_0_crlf}" "${first_changelog_crlf}" root)
run_prepare(run "${root}" 0.1.0 2026-11-13 "")
expect_release("the first release with CRLF" run "${root}"
    "${header_0_1_0_crlf}" "${first_expected_crlf}")

# --- A pre-release after the first release -------------------------------------

header_text(0 2 0 "rc.1" header_rc)
fixture_text(CHANGELOG-second-release.md second_changelog)
fixture_text(CHANGELOG-second-release-expected.md second_expected)

# Only v0.1.0 is a release tag. The other lines are not tags of the form
# v<version>, even those that would be higher than 0.2.0-rc.1.
string(CONCAT tags
    "v0.1.0\r\n"
    "\n"
    "  \n"
    "1.0.0\n"
    "v1.0\n"
    "v1.0.0+build\n"
    "vnext\n"
    "release-1.0.0\n")
make_tree(pre "${header_0_1_0}" "${second_changelog}" root)
run_prepare(run "${root}" 0.2.0-rc.1 2026-12-01 "${tags}")
expect_release("a pre-release" run "${root}" "${header_rc}" "${second_expected}")
argx_test_expect_equal("${run_FILE}"
    "earlier=value\nversion=0.2.0-rc.1\ntag=v0.2.0-rc.1\nprerelease=true\nbreaking=false\n"
    "a pre-release: the output file")

# The same with CRLF line endings.
to_crlf("${header_rc}" header_rc_crlf)
to_crlf("${second_changelog}" second_changelog_crlf)
to_crlf("${second_expected}" second_expected_crlf)
make_tree(pre_crlf "${header_0_1_0_crlf}" "${second_changelog_crlf}" root)
run_prepare(run "${root}" 0.2.0-rc.1 2026-12-01 "v0.1.0\n")
expect_release("a pre-release with CRLF" run "${root}" "${header_rc_crlf}" "${second_expected_crlf}")

# Without an output file, the script still rewrites the two files.
make_tree(no_output "${header_0_1_0}" "${second_changelog}" root)
file(WRITE "${root}.tags" "v0.1.0\n")
argx_test_run_script(run "${script}"
    "-DARGX_RELEASE_VERSION=0.2.0-rc.1"
    "-DARGX_RELEASE_DATE=2026-12-01"
    "-DARGX_RELEASE_REPO_URL=${repo_url}"
    "-DARGX_SOURCE_DIR=${root}"
    "-DARGX_RELEASE_TAGS_FILE=${root}.tags")
expect_release("a pre-release without an output file" run "${root}" "${header_rc}" "${second_expected}")

# --- Check only ------------------------------------------------------------------

# The checks pass, but neither the tree nor the output file is written.
make_tree(check "${header_0_1_0}" "${first_changelog}" root)
tree_snapshot("${root}" before)
run_prepare(run "${root}" 0.1.0 2026-11-13 "" "-DARGX_RELEASE_CHECK_ONLY=ON")
argx_test_expect_equal("${run_RESULT}" "0" "check only: the script succeeds")
tree_snapshot("${root}" after)
argx_test_expect_equal("${after}" "${before}" "check only: the tree is not written")
argx_test_expect_equal("${run_FILE}" "earlier=value\n" "check only: the output file is not written")
argx_test_expect_contains("${run_OUTPUT}" "0.1.0" "check only: the standard output names the version")

# --- Failures ----------------------------------------------------------------------

# Makes the tree <name> from <header> and <changelog>, runs the script with
# <version> and <tags> (and the date 2026-12-01, unless an argument sets
# another) and checks that it fails with "error: " and <reason> in its output,
# and that neither the tree nor the output file has changed. Sets run_OUTPUT
# to the flattened output of the run.
function(expect_failure what name header changelog version tags reason)
    make_tree(${name} "${header}" "${changelog}" root)
    tree_snapshot("${root}" before)
    run_prepare(run "${root}" "${version}" 2026-12-01 "${tags}" ${ARGN})
    if("${run_RESULT}" STREQUAL "0")
        argx_test_expect_equal("${run_RESULT}" "non-zero" "${what}: the script fails")
    else()
        argx_test_expect_equal("failed" "failed" "${what}: the script fails")
    endif()
    argx_test_expect_contains("${run_OUTPUT}" "error: " "${what}: the output starts the reason with error:")
    argx_test_expect_contains("${run_OUTPUT}" "${reason}" "${what}: the reason")
    tree_snapshot("${root}" after)
    argx_test_expect_equal("${after}" "${before}" "${what}: the tree is not written")
    argx_test_expect_equal("${run_FILE}" "earlier=value\n" "${what}: the output file is not written")
    set(run_OUTPUT "${run_OUTPUT}" PARENT_SCOPE)
endfunction()

# Missing inputs and a date of another form.
set(index 0)
foreach(input IN ITEMS ARGX_RELEASE_VERSION ARGX_RELEASE_DATE ARGX_RELEASE_REPO_URL
        ARGX_SOURCE_DIR ARGX_RELEASE_TAGS_FILE)
    math(EXPR index "${index} + 1")
    expect_failure("${input} is empty" missing_${index} "${header_0_1_0}" "${second_changelog}"
        0.2.0 "v0.1.0\n" "${input} is not set" "-D${input}=")
endforeach()
foreach(date IN ITEMS 2026-1-01 26-12-01 2026/12/01 2026-12-01T00:00)
    string(MAKE_C_IDENTIFIER "${date}" id)
    expect_failure("the date \"${date}\"" date_${id} "${header_0_1_0}" "${second_changelog}"
        0.2.0 "v0.1.0\n" "is not of the form YYYY-MM-DD" "-DARGX_RELEASE_DATE=${date}")
endforeach()
expect_failure("the tags file does not exist" no_tags "${header_0_1_0}" "${second_changelog}"
    0.2.0 "" "does not exist" "-DARGX_RELEASE_TAGS_FILE=${ARGX_TEST_WORK_DIR}/none.txt")
expect_failure("the source tree does not exist" no_tree "${header_0_1_0}" "${second_changelog}"
    0.2.0 "" "does not exist" "-DARGX_SOURCE_DIR=${ARGX_TEST_WORK_DIR}/none")

# Versions that are not of the form <major>.<minor>.<patch>[-<pre-release>],
# build metadata included, and numbers beyond what the header can hold.
foreach(version IN ITEMS 0.2 v0.2.0 01.2.0 0.2.0+build 0.2.0-rc.1+build 0.2.0- 0.2.0-rc..1
        256.0.0 0.256.0 0.2.256)
    string(MAKE_C_IDENTIFIER "${version}" id)
    expect_failure("the version ${version}" version_${id} "${header_0_1_0}" "${second_changelog}"
        "${version}" "v0.1.0\n" "${version}")
endforeach()
expect_failure("the version 0.2.0+build" build_metadata "${header_0_1_0}" "${second_changelog}"
    0.2.0+build "v0.1.0\n" "build metadata")
expect_failure("the version 256.0.0" range "${header_0_1_0}" "${second_changelog}"
    256.0.0 "v0.1.0\n" "0-255")

# The tag of the version exists already, also when the version is that of the
# header (which is accepted only while it has no tag).
expect_failure("the tag exists" tag_exists "${header_0_1_0}" "${second_changelog}"
    0.2.0 "v0.1.0\nv0.2.0\n" "the tag v0.2.0 already exists")
expect_failure("the tag of the header version exists" header_tag_exists "${header_0_1_0}"
    "${first_changelog}" 0.1.0 "v0.1.0\n" "the tag v0.1.0 already exists")

# The version is not higher than every release tag.
expect_failure("lower than a tag" lower_than_tag "${header_0_1_0}" "${second_changelog}"
    0.2.0 "v0.1.0\nv0.3.0\n" "is not higher than the release tag v0.3.0")
expect_failure("lower than a pre-release tag" lower_than_pre_tag "${header_0_1_0}"
    "${second_changelog}" 0.2.0-rc.1 "v0.1.0\nv0.2.0-rc.2\n"
    "is not higher than the release tag v0.2.0-rc.2")

# The version is lower than the header.
header_text(0 2 0 "" header_0_2_0)
expect_failure("lower than the header" lower_than_header "${header_0_2_0}" "${second_changelog}"
    0.1.5 "v0.1.0\n" "is lower than the version 0.2.0 of the version header")
expect_failure("lower than the pre-release of the header" lower_than_header_pre "${header_rc}"
    "${second_changelog}" 0.2.0-beta.1 "v0.1.0\n"
    "is lower than the version 0.2.0-rc.1 of the version header")

# The changelog is not of the accepted form, has no entry under [Unreleased],
# or lists the version already.
fixture_text(CHANGELOG-invalid.md invalid_changelog)
fixture_text(CHANGELOG-no-entries.md no_entries_changelog)
expect_failure("an invalid changelog" changelog_invalid "${header_0_1_0}" "${invalid_changelog}"
    0.2.0 "" "CHANGELOG.md is not valid")
expect_failure("no entry under [Unreleased]" changelog_no_entries "${header_0_1_0}"
    "${no_entries_changelog}" 0.2.0 "v0.1.0\n" "[Unreleased] has no entry")
expect_failure("the version is listed" changelog_listed "${header_0_1_0}" "${second_expected}"
    0.2.0-rc.1 "v0.1.0\n" "0.2.0-rc.1 is already listed in")
# The changelog lists 0.2.0-rc.1, which has no tag, and has an entry under
# [Unreleased]: 0.2.0-alpha passes the checks of the tags and the header, but
# cannot go below the newest version of the changelog.
string(REPLACE "## [Unreleased]\n" "## [Unreleased]\n\n### Fixed\n\n- A fix.\n"
    higher_changelog "${second_expected}")
expect_failure("the changelog lists a higher version" changelog_higher "${header_0_1_0}"
    "${higher_changelog}" 0.2.0-alpha "v0.1.0\n" "is not higher than 0.2.0-rc.1")

# Written as on Windows, a changelog without a line ending at its end cannot be
# kept byte for byte: the script stops before writing either file.
string(REGEX REPLACE "\n+$" "" no_newline_changelog "${second_changelog}")
expect_failure("no line ending at the end, as on Windows" no_newline "${header_0_1_0}"
    "${no_newline_changelog}" 0.2.0-rc.1 "v0.1.0\n" "ends with a line ending"
    "-DARGX_WRITE_BYTES_WITH_CONFIGURE=ON")
expect_failure("no line ending at the end, check only" no_newline_check "${header_0_1_0}"
    "${no_newline_changelog}" 0.2.0-rc.1 "v0.1.0\n" "ends with a line ending"
    "-DARGX_WRITE_BYTES_WITH_CONFIGURE=ON" "-DARGX_RELEASE_CHECK_ONLY=ON")

# --- The version against the entries ----------------------------------------------

# Sets <out-var> to a changelog with <body> under [Unreleased] and a released
# 0.1.0. The body starts on line 5.
function(entries_changelog body out_var)
    set(${out_var} "# Changelog\n\n## [Unreleased]\n\n${body}\n## [0.1.0] - 2026-11-13\n\n\
### Added\n\n- The first release.\n\n[Unreleased]: ${repo_url}/compare/v0.1.0...HEAD\n\
[0.1.0]: ${repo_url}/releases/tag/v0.1.0\n" PARENT_SCOPE)
endfunction()

# Makes the tree <name> from <header> and a changelog with <body> under
# [Unreleased], runs the script with <version> and <tags>, and checks that it
# succeeds, prints <message> and writes breaking=<breaking> to the output file.
function(expect_allowed what name header body version tags breaking message)
    entries_changelog("${body}" changelog)
    make_tree(${name} "${header}" "${changelog}" root)
    run_prepare(run "${root}" "${version}" 2026-12-01 "${tags}")
    argx_test_expect_equal("${run_RESULT}" "0" "${what}: the script succeeds")
    argx_test_expect_contains("${run_FILE}" "\nbreaking=${breaking}\n"
        "${what}: the output file has breaking=${breaking}")
    argx_test_expect_contains("${run_OUTPUT}" "${message}" "${what}: the kind of the release")
endfunction()

# The same for a run that must fail with <reason>.
function(expect_refused what name header body version tags reason)
    entries_changelog("${body}" changelog)
    expect_failure("${what}" ${name} "${header}" "${changelog}" "${version}" "${tags}"
        "${reason}" ${ARGN})
    argx_test_expect_contains("${run_OUTPUT}" "do not allow the version ${version}"
        "${what}: the version is named")
endfunction()

header_text(1 2 0 "" header_1_2_0)
set(fixed_body "### Fixed\n\n- A fix.\n\n### Security\n\n- A security fix.\n")
set(breaking_body "### Changed\n\n- **BREAKING**: `argx_foo()` is renamed `argx_bar()`. \
Rename the calls.\n")

# Allowed versions.
expect_allowed("a patch with Fixed and Security" allowed_patch "${header_0_1_0}" "${fixed_body}"
    0.1.1 "v0.1.0\n" false "A patch release after 0.1.0, with 0 **BREAKING** entries.")
expect_allowed("a breaking change before 1.0" allowed_minor_0 "${header_0_1_0}"
    "${breaking_body}" 0.2.0 "v0.1.0\n" true "A minor release after 0.1.0")
expect_allowed("an addition after 1.0" allowed_minor_1 "${header_1_2_0}"
    "### Added\n\n- A function.\n" 1.3.0 "v1.2.0\n" false "A minor release after 1.2.0")
string(REPLACE "Changed" "Removed" removed_body "${breaking_body}")
expect_allowed("a breaking change in a major release" allowed_major "${header_1_2_0}"
    "${removed_body}" 2.0.0 "v1.2.0\n" true "A major release after 1.2.0")
expect_allowed("a pre-release of a major release" allowed_major_pre "${header_1_2_0}"
    "${breaking_body}" 2.0.0-rc.1 "v1.2.0\n" true "A major release after 1.2.0")
# The pre-release tags are not the base: 0.2.0 after 0.2.0-rc.1 is checked as a
# minor release after 0.1.0.
expect_allowed("a release after its pre-release" allowed_after_pre "${header_rc}"
    "### Added\n\n- A function.\n" 0.2.0 "v0.1.0\nv0.2.0-rc.1\n" false
    "A minor release after 0.1.0")
# The first release is not checked against the entries, only for their form.
set(first_entries_changelog
    "# Changelog\n\n## [Unreleased]\n\n${breaking_body}\n[Unreleased]: ${repo_url}/commits/main\n")
make_tree(allowed_first "${header_0_1_0}" "${first_entries_changelog}" root)
run_prepare(run "${root}" 0.1.0 2026-12-01 "")
argx_test_expect_equal("${run_RESULT}" "0" "the first release with a breaking change: the script succeeds")
argx_test_expect_contains("${run_FILE}" "\nbreaking=true\n"
    "the first release with a breaking change: the output file has breaking=true")
argx_test_expect_contains("${run_OUTPUT}" "The first release"
    "the first release with a breaking change: the kind of the release")
string(REPLACE "### Changed\n\n" "" first_form_changelog "${first_entries_changelog}")
expect_failure("an entry under no category in the first release" refused_first_form
    "${header_0_1_0}" "${first_form_changelog}" 0.1.0 ""
    "line 5: the entry is under no category heading")

# A patch release has Fixed and Security entries only.
foreach(category IN ITEMS Added Changed Deprecated Removed)
    expect_refused("a patch with ${category}" refused_patch_${category} "${header_0_1_0}"
        "### Fixed\n\n- A fix.\n\n### ${category}\n\n- A change.\n" 0.1.1 "v0.1.0\n"
        "the version 0.1.1 is a patch release after 0.1.0, but line 11 is an entry under \
${category}, which needs at least a minor release; a patch release may have Fixed and Security \
entries only. Release 0.2.0 or higher.")
endforeach()
expect_refused("a patch with a breaking change" refused_patch_breaking "${header_0_1_0}"
    "${breaking_body}" 0.1.1 "v0.1.0\n"
    "line 7 is a **BREAKING** entry, which needs at least a minor release. Release 0.2.0 or higher.")
expect_refused("a pre-release of a patch with Added" refused_patch_pre "${header_1_2_0}"
    "### Added\n\n- A function.\n" 1.2.1-rc.1 "v1.2.0\n"
    "the version 1.2.1-rc.1 is a patch release after 1.2.0")
expect_refused("a patch after a pre-release tag" refused_patch_after_pre "${header_0_1_0}"
    "### Added\n\n- A function.\n" 0.1.1 "v0.1.0\nv0.1.1-rc.1\n"
    "the version 0.1.1 is a patch release after 0.1.0")
# Without the tag v0.1.0, as in a clone without tags, the release 0.1.0 listed
# in the changelog is the base.
expect_refused("a patch without the tags" refused_patch_no_tags "${header_0_1_0}"
    "### Added\n\n- A function.\n" 0.1.1 "" "the version 0.1.1 is a patch release after 0.1.0")
expect_allowed("a patch without the tags, with a fix" allowed_patch_no_tags "${header_0_1_0}"
    "${fixed_body}" 0.1.1 "" false "A patch release after 0.1.0")
expect_refused("a patch, check only" refused_patch_check "${header_0_1_0}"
    "### Added\n\n- A function.\n" 0.1.1 "v0.1.0\n" "is a patch release after 0.1.0"
    "-DARGX_RELEASE_CHECK_ONLY=ON")

# From 1.0 on, a breaking change needs a major release.
expect_refused("a breaking change in a minor release after 1.0" refused_minor_breaking
    "${header_1_2_0}" "${breaking_body}" 1.3.0 "v1.2.0\n"
    "the version 1.3.0 is a minor release after 1.2.0, but line 7 is a **BREAKING** entry, \
which needs a major release from 1.0 on. Release 2.0.0 or higher.")

# The form of the entries, whatever the version.
expect_refused("a breaking change under Fixed" refused_marker_place "${header_0_1_0}"
    "### Fixed\n\n- **BREAKING**: A fix that changes the API.\n" 0.2.0 "v0.1.0\n"
    "line 7: the **BREAKING** entry is under Fixed. A breaking change goes under Changed or Removed.")
foreach(marker IN ITEMS "**Breaking**:" "**BREAKING:**" "BREAKING:" "__BREAKING__:")
    string(MAKE_C_IDENTIFIER "${marker}" id)
    expect_refused("the marker ${marker}" refused_marker_${id} "${header_0_1_0}"
        "### Changed\n\n- ${marker} A change.\n" 0.2.0 "v0.1.0\n"
        "line 7: the entry starts with a marker close to \"**BREAKING**: \"")
endforeach()
expect_refused("an entry under no category" refused_no_category "${header_0_1_0}"
    "- A change.\n\n### Fixed\n\n- A fix.\n" 0.2.0 "v0.1.0\n"
    "line 5: the entry is under no category heading")
expect_refused("a category without entries" refused_empty_heading "${header_0_1_0}"
    "### Added\n\n### Fixed\n\n- A fix.\n" 0.1.1 "v0.1.0\n"
    "line 5: the category heading has no entry under it")

# Running the script twice with the same input: the second run stops, since
# the changelog lists the version now.
make_tree(twice "${header_0_1_0}" "${second_changelog}" root)
run_prepare(run "${root}" 0.2.0-rc.1 2026-12-01 "v0.1.0\n")
argx_test_expect_equal("${run_RESULT}" "0" "twice: the first run succeeds")
tree_snapshot("${root}" before)
run_prepare(run "${root}" 0.2.0-rc.1 2026-12-01 "v0.1.0\n")
if("${run_RESULT}" STREQUAL "0")
    argx_test_expect_equal("${run_RESULT}" "non-zero" "twice: the second run fails")
endif()
argx_test_expect_contains("${run_OUTPUT}" "0.2.0-rc.1 is already listed in"
    "twice: the second run names the reason")
tree_snapshot("${root}" after)
argx_test_expect_equal("${after}" "${before}" "twice: the second run writes nothing")

argx_test_finish()
