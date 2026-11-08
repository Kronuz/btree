#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_build=${BTREE_TEST_BUILD:-"$test_root/.scratch/node-layout"}
test_include=${BTREE_TEST_INCLUDE_ROOT:-"$test_root"}
mkdir -p "$test_build"
test_compiler=${CXX:-c++}
for standard in 17 20; do
    for mode in release debug; do
        case "$mode" in release) diagnostic=-DNDEBUG ;; debug) diagnostic=-UNDEBUG ;; esac
        output="$test_build/cxx$standard-$mode"
        if [ "${BTREE_TEST_SANITIZE:-0}" = 1 ]; then
            test_flags="-O1 -g -fsanitize=address,undefined,object-size,pointer-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer"
        else
            test_flags="-O2"
        fi
        for source in node_layout drain_noalloc; do
            suffix=""
            if [ "$source" != node_layout ]; then suffix="-$source"; fi
            "$test_compiler" -std=c++"$standard" $test_flags "$diagnostic" -I "$test_include" "$test_root/tests/$source.cc" -o "$output$suffix"
        done
        if [ -n "${BTREE_TEST_CASE:-}" ]; then
            "$output" "$BTREE_TEST_CASE"
        else
            "$output"
            "$output-drain_noalloc"
        fi
        printf 'PASS compact layout C++%s %s\n' "$standard" "$mode"
    done
done
