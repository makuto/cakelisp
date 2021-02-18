#!/bin/sh

# Make sure Cakelisp is up to date
. ./Build.sh || exit $?

./bin/cakelisp runtime/Config_Linux.cake test/RunTests.cake || exit $?

# TODO: Add precompiled headers to build system. They are a 60% speedup for compile-time building
# See https://clang.llvm.org/docs/UsersManual.html#precompiled-headers
# clang -g -fPIC -x c++-header src/Evaluator.hpp -o src/Evaluator.hpp.pch

echo "\n============================\nHot reloadable library\n"

./bin/cakelisp \
	runtime/Config_Linux.cake runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake || exit $?

# TestMain is the loader. It doesn't care at all about fancy hot reloading macros, it just loads libs
# ./bin/cakelisp \
	# runtime/Config_Windows.cake runtime/HotLoader.cake  || exit $?

echo "\n============================\nHot loader\n"

./bin/cakelisp \
	runtime/Config_Linux.cake runtime/HotLoader.cake || exit $?

# ./bin/cakelisp CrossCompile_Windows.cake || exit $?
