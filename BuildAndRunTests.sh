#!/bin/sh

# Make sure Cakelisp is up to date
. ./Build.sh || exit $?

# TODO: Add precompiled headers to build system. They are a 60% speedup for compile-time building
# See https://clang.llvm.org/docs/UsersManual.html#precompiled-headers
# clang -g -fPIC -x c++-header src/Evaluator.hpp -o src/Evaluator.hpp.pch

./bin/cakelisp --execute \
						  test/CodeModification.cake

./bin/cakelisp \
	test/BuildOptions.cake

./bin/cakelisp --execute \
	test/Execute.cake

./bin/cakelisp \
	runtime/Config_Linux.cake runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake || exit $?

# TestMain is the loader. It doesn't care at all about fancy hot reloading macros, it just loads libs
./bin/cakelisp \
	runtime/Config_Windows.cake runtime/HotLoader.cake  || exit $?

./bin/cakelisp \
	runtime/Config_Linux.cake runtime/HotLoader.cake || exit $?

./bin/cakelisp --execute \
	test/Defines.cake

# ./bin/cakelisp CrossCompile_Windows.cake

./bin/cakelisp --execute test/MultiLineStrings.cake || exit $?
