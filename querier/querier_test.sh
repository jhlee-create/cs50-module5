#!/bin/bash

# Run each querier error and input case and print PASS or FAIL.

if [ "$#" -ne 2 ]; then
    echo "usage: querier_test.sh <pageDirectory> <indexFile>"
    exit 1
fi

pagedir=$1
indexfile=$2
script_dir=$(cd "$(dirname "$0")" && pwd)
query="$script_dir/query"
test_dir=$(mktemp -d /tmp/querier-test.XXXXXX) || exit 1
passed=0
failed=0

# Restore permissions and remove all temporary test files when testing ends.
cleanup()
{
    chmod 700 "$test_dir/inaccessible" 2>/dev/null
    chmod 600 "$test_dir/unreadable.index" 2>/dev/null
    rm -rf -- "$test_dir"
}
trap cleanup EXIT HUP INT TERM

# Stop if the query program, crawler pages, or index cannot be used.
if [ ! -x "$query" ]; then
    echo "querier_test: $query is not executable"
    exit 1
fi
if [ ! -d "$pagedir" ]; then
    echo "querier_test: $pagedir is not a directory"
    exit 1
fi
if [ ! -r "$indexfile" ] || [ ! -s "$indexfile" ]; then
    echo "querier_test: $indexfile is not a readable, nonempty file"
    exit 1
fi

# Choose one indexed word so valid queries always match a saved page.
read -r valid_word remaining_fields < "$indexfile"
if [ -z "$valid_word" ]; then
    echo "querier_test: could not read a word from $indexfile"
    exit 1
fi
missing_word=zzzznotawordzzzz

# Run one case and compare its exit status and output with expectations.
run_case()
{
    name=$1
    expected_status=$2
    input_file=$3
    required_text=$4
    forbidden_text=$5
    shift 5

    # Save normal and error output so both can be checked.
    "$@" < "$input_file" \
        > "$test_dir/stdout" 2> "$test_dir/stderr"
    actual_status=$?
    cat "$test_dir/stdout" "$test_dir/stderr" > "$test_dir/output"
    case_passed=true

    # Confirm that the command succeeded or failed as expected.
    if [ "$expected_status" = "zero" ] && [ "$actual_status" -ne 0 ]; then
        case_passed=false
    fi
    if [ "$expected_status" = "nonzero" ] && [ "$actual_status" -eq 0 ]; then
        case_passed=false
    fi

    # Confirm that required text appears, or that the output is empty.
    if [ "$required_text" = "EMPTY" ]; then
        if [ -s "$test_dir/output" ]; then
            case_passed=false
        fi
    elif [ -n "$required_text" ] &&
         ! grep -Fq -- "$required_text" "$test_dir/output"; then
        case_passed=false
    fi

    # Reject output containing text that should not appear.
    if [ -n "$forbidden_text" ] &&
       grep -Fq -- "$forbidden_text" "$test_dir/output"; then
        case_passed=false
    fi

    if [ "$case_passed" = true ]; then
        echo "PASS: $name"
        passed=$((passed + 1))
    else
        echo "FAIL: $name"
        echo "  exit status: $actual_status"
        sed 's/^/  /' "$test_dir/output"
        failed=$((failed + 1))
    fi
}

# Create an uncrawled directory, an inaccessible directory, and an unreadable index.
mkdir "$test_dir/not-crawled"
mkdir "$test_dir/inaccessible"
chmod 000 "$test_dir/inaccessible"
echo "unreadable" > "$test_dir/unreadable.index"
chmod 000 "$test_dir/unreadable.index"

# Create the exact empty, newline, tab, missing-word, and operator inputs.
: > "$test_dir/empty.in"
printf '\n' > "$test_dir/enter.in"
printf '\t\n' > "$test_dir/tab.in"
printf '%s\n' "$missing_word" > "$test_dir/missing.in"
printf '%s or %s\n' "$valid_word" "$missing_word" \
    > "$test_dir/or-missing.in"
printf '%s and %s\n' "$valid_word" "$missing_word" \
    > "$test_dir/and-missing.in"
printf '%s  or\t%s and\t%s\n' \
    "$valid_word" "$missing_word" "$valid_word" \
    > "$test_dir/mixed.in"
printf 'and %s\n' "$valid_word" > "$test_dir/and-first.in"
printf 'or %s\n' "$valid_word" > "$test_dir/or-first.in"
printf '\tand %s\n' "$valid_word" > "$test_dir/tab-and-first.in"
printf '%s and\n' "$valid_word" > "$test_dir/and-last.in"
printf '%s or\n' "$valid_word" > "$test_dir/or-last.in"
printf '%s or\t\n' "$valid_word" > "$test_dir/or-tab-last.in"
printf '%s and or %s\n' "$valid_word" "$valid_word" \
    > "$test_dir/and-or-middle.in"
printf '%s and and %s\n' "$valid_word" "$valid_word" \
    > "$test_dir/and-and-middle.in"
printf '%s or and %s\n' "$valid_word" "$valid_word" \
    > "$test_dir/or-and-middle.in"
printf '%s\n' "$valid_word" > "$test_dir/valid.in"

# Check too few and too many command-line parameters.
run_case "invalid parameters: too few" nonzero "$test_dir/empty.in" \
    "usage: query <pageDirectory> <indexFile> [-q]" "" \
    "$query"
run_case "invalid parameters: too many" nonzero "$test_dir/empty.in" \
    "usage: query <pageDirectory> <indexFile> [-q]" "" \
    "$query" "$pagedir" "$indexfile" -q extra

# Check uncrawled, inaccessible, and unreadable paths.
run_case "directory that has not been crawled" nonzero \
    "$test_dir/empty.in" "query: directory does not contain crawler pages" "" \
    "$query" "$test_dir/not-crawled" "$indexfile" -q
run_case "directory that is not accessible" nonzero \
    "$test_dir/empty.in" "query: page directory is not accessible" "" \
    "$query" "$test_dir/inaccessible" "$indexfile" -q
run_case "index file that is not readable" nonzero \
    "$test_dir/empty.in" "query: index file is not readable" "" \
    "$query" "$pagedir" "$test_dir/unreadable.index" -q

# Load the supplied large index and find a word known to be present.
index_lines=$(wc -l < "$indexfile")
echo "INFO: large index contains $index_lines word entries"
run_case "large index file" zero "$test_dir/valid.in" \
    "rank:" "[invalid query]" \
    "$query" "$pagedir" "$indexfile" -q

# Check EOF with no input, Enter-only input, and Tab-only input.
run_case "empty input followed by EOF" zero "$test_dir/empty.in" \
    EMPTY "" "$query" "$pagedir" "$indexfile" -q
run_case "Enter followed by EOF" zero "$test_dir/enter.in" \
    EMPTY "" "$query" "$pagedir" "$indexfile" -q
run_case "Tab followed by EOF" zero "$test_dir/tab.in" \
    EMPTY "" "$query" "$pagedir" "$indexfile" -q

# Check a missing word alone and combined with a valid word.
run_case "single word that does not exist" zero "$test_dir/missing.in" \
    "$missing_word" "rank:" "$query" "$pagedir" "$indexfile" -q
run_case "valid word OR missing word" zero "$test_dir/or-missing.in" \
    "rank:" "[invalid query]" "$query" "$pagedir" "$indexfile" -q
run_case "valid word AND missing word" zero "$test_dir/and-missing.in" \
    "$valid_word and $missing_word" "rank:" \
    "$query" "$pagedir" "$indexfile" -q

# Check mixed AND, OR, spaces, tabs, valid words, and a missing word.
run_case "mixed operators, spaces, tabs, valid, and missing words" zero \
    "$test_dir/mixed.in" "rank:" "[invalid query]" \
    "$query" "$pagedir" "$indexfile" -q

# Check operators at the beginning and end of a query.
run_case "AND at the beginning" zero "$test_dir/and-first.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "OR at the beginning" zero "$test_dir/or-first.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "Tab then AND at the beginning" zero "$test_dir/tab-and-first.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "AND at the end" zero "$test_dir/and-last.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "OR at the end" zero "$test_dir/or-last.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "OR then Tab at the end" zero "$test_dir/or-tab-last.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q

# Check every adjacent AND and OR combination in the middle of a query.
run_case "adjacent AND OR" zero "$test_dir/and-or-middle.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "adjacent AND AND" zero "$test_dir/and-and-middle.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q
run_case "adjacent OR AND" zero "$test_dir/or-and-middle.in" \
    "[invalid query]" "" "$query" "$pagedir" "$indexfile" -q

# Confirm that a valid query with no matches prints no ranked pages.
run_case "no scores returned" zero "$test_dir/missing.in" \
    "$missing_word" "rank:" "$query" "$pagedir" "$indexfile" -q

# Report whether every case behaved as expected.
echo "$passed tests passed; $failed tests failed"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0
