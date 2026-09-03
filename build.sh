#!/bin/sh -e

# shellcheck disable=SC2086

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

# shellcheck source=./cbase/common.sh
. "./cbase/common.sh"

program=$(common_get_program "$0")
script=$(basename "$0")


common_build_parse_args "$@"

case "$mode" in
benchmark|build|callgrind|check|cross|debug|debug-fast|fast_feedback|install|perf|release|test|test_all|uninstall)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

common_build_print_invocation "$script"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"

exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CC=$(common_get_compiler "$mode")

CPPFLAGS="$CPPFLAGS -Icbase"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"

LDFLAGS="$LDFLAGS -lm -lmagic"

case "$mode" in
debug)
    CFLAGS="$CFLAGS -g3"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/$program"
    ;;
debug-fast)
    CFLAGS="$CFLAGS -g2 -O2 -flto -march=native -ftree-vectorize"
    CFLAGS="$CFLAGS -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
benchmark)
    CFLAGS="$CFLAGS -O2 -flto -march=native -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS -DBRN2_BENCHMARK=1"
    exe="bin/$program"
    ;;
perf)
    CFLAGS="$CFLAGS -g3 -Og -flto"
    CPPFLAGS="$CPPFLAGS -DBRN2_BENCHMARK=1"
    exe="bin/$program"
    ;;
callgrind)
    CFLAGS="$CFLAGS -g3 -O2 -ftree-vectorize"
    ;;
test)
    CFLAGS="$CFLAGS -g3 -DDEBUGGING=1"
    ;;
check)
    CC=gcc
    CFLAGS="$CFLAGS -DDEBUGGING=1 -fanalyzer"
    ;;
build)
    CFLAGS="$CFLAGS -O2 -flto -march=native -ftree-vectorize"
    ;;
release)
    CFLAGS="$CFLAGS -DRELEASING=1 -O2 -flto -march=native -ftree-vectorize"
    ;;
fast_feedback)
    ;;

cross)
    common_build_cross_all
    CFLAGS="$CFLAGS -O2"
    ;;
benchmark|build|callgrind|check|cross|debug|debug-fast|fast_feedback|install|perf|release|test|test_all|uninstall)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

if [ "$mode" = "cross" ]; then
    cross="$target"
    CC="zig cc"
    CFLAGS="$CFLAGS -target $cross"

    case $cross in
    x86_64-macos|aarch64-macos)
        CFLAGS="$CFLAGS -fno-lto"
        LDFLAGS="$LDFLAGS -lpthread"
        ;;
    *windows*)
        exe="bin/$program.exe"
        ;;
    *)
        LDFLAGS="$LDFLAGS -lpthread"
        ;;
    esac
else
    LDFLAGS="$LDFLAGS -lpthread"
fi

case "$mode" in
fast_feedback)
    trace_on
    $CC $CPPFLAGS $CFLAGS main.c -o "$exe" $LDFLAGS && LC_ALL=C "$exe"
    trace_off
    ;;
uninstall)
    trace_on
    rm -f ${DESTDIR}${PREFIX}/bin/${program}
    rm -f ${DESTDIR}${PREFIX}/man/man1/${program}.1
    exit
    ;;
install)
    trace_on
    if [ ! -f "$program" ]; then
        $0 build
    fi
    common_install_opt -Dm755 bin/${program}   ${DESTDIR}${PREFIX}/bin/${program}
    common_install_opt -Dm644 ${program}.1 ${DESTDIR}${PREFIX}/man/man1/${program}.1
    common_install_opt -dm755 etc/ "$DESTDIR/etc/$program"
    common_install_opt -Dm755 \
        "$program.desktop" \
        "$DESTDIR/usr/share/applications/$program.desktop"
    trace_off
    exit
    ;;
test)
    common_test "$target"
    exit
    ;;
test_all)
    ;;
benchmark|build|callgrind|cross|debug|debug-fast|perf|release)
    common_build_tags
    trace_on
    $CC $CPPFLAGS $CFLAGS $LDFLAGS -o ${exe} main.c
    trace_off
    ;;
esac

case "$mode" in
check)
    set +e
    CC=gcc CFLAGS="-fanalyzer" ./build.sh

    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CC=clang CFLAGS="$CFLAGS" ./build.sh
    exit
    ;;
esac

trace_off
if [ "$mode" = "test_all" ]; then
    common_build_test_all "debug build test" gcc tcc clang "zig cc"
fi
