#!/bin/sh -e

# shellcheck disable=SC2086

dir=$(dirname "$(readlink -f "$0")")
# shellcheck source=/dev/null
. "$dir/cbase/common.sh"

cbase="cbase"
cd "$dir" || exit
program=$(get_program "$0")
script=$(basename "$0")

if [ -f ./targets ]; then
    . ./targets
else
    targets=$(cat <<'EOF_TARGETS'
build
debug
fast_feedback
install
uninstall
test
check
release
benchmark
perf
valgrind
callgrind
test_all
cross x86_64-linux
cross aarch64-linux
cross x86_64-macos
cross aarch64-macos
cross x86_64-windows-gnu
EOF_TARGETS
)
fi

target="${1:-debug}"
target_line=$target
if [ "$target" = "cross" ] && [ -n "${2:-}" ]; then
    target_line="$target $2"
fi

if ! target_supported "$targets" "$target_line" \
        && ! target_supported "$targets" "$target"; then
    echo "usage: $script <targets>"
    printf '%s\n' "$targets"
    exit 1
fi

printf "\n${script} ${RED}${1:-} ${2:-}$RES\n"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"

exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CC=$(get_compiler "$target")

CPPFLAGS="$CPPFLAGS -I$dir/$cbase"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Werror=all -Werror=extra"
CFLAGS="$CFLAGS -Werror"  # Only uncomment occasionally, keep this line

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-cast-qual"
    CFLAGS="$CFLAGS -Wno-constant-logical-operand"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-float-equal"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-unused-macros"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"
fi

LDFLAGS="$LDFLAGS -lm -lmagic"

case "$target" in
debug)
    CFLAGS="$CFLAGS -g3 -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
benchmark)
    CFLAGS="$CFLAGS -O2 -flto -march=native -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS -DBRN2_BENCHMARK=1"
    exe="bin/${program}_benchmark"
    ;;
perf)
    CFLAGS="$CFLAGS -g3 -Og -flto"
    CPPFLAGS="$CPPFLAGS -DBRN2_BENCHMARK=1"
    exe="bin/${program}_perf"
    ;;
valgrind)
    CFLAGS="$CFLAGS -g3 -Og -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
callgrind)
    CFLAGS="$CFLAGS -g3 -O2 -ftree-vectorize"
    ;;
test)
    CFLAGS="$CFLAGS -g3 -DDEBUGGING=1 -fsanitize=undefined"
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

*)
    CFLAGS="$CFLAGS -O2"
    ;;
esac

if [ "$target" = "cross" ]; then
    cross="$2"
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

case "$target" in
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
    install_opt -Dm755 bin/${program}   ${DESTDIR}${PREFIX}/bin/${program}
    install_opt -Dm644 ${program}.1 ${DESTDIR}${PREFIX}/man/man1/${program}.1
    install_opt -dm755 etc/ "$DESTDIR/etc/$program"
    install_opt -Dm755 \
        "$program.desktop" \
        "$DESTDIR/usr/share/applications/$program.desktop"
    trace_off
    exit
    ;;
test)
    test "$2"
    exit
    ;;
test_all)
    ;;
*)
    trace_on
    build_tags
    $CC $CPPFLAGS $CFLAGS $LDFLAGS -o ${exe} main.c
    trace_off
    ;;
esac

case "$target" in
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
if [ "$target" = "test_all" ]; then
    printf '%s\n' "$targets" | while IFS= read -r target; do
        echo "$target" | grep -Eq "^(# |$)" && continue
        if echo "$target" | grep "cross"; then
            $0 $target
            continue
        fi
        for compiler in gcc tcc clang "zig cc" ; do
            CC=$compiler $0 $target || exit
        done
    done
fi
