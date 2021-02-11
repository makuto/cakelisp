#!/bin/sh

# Make sure Cakelisp is up to date
. ./Build.sh || exit $?

# TODO: Add precompiled headers to build system. They are a 60% speedup for compile-time building
# See https://clang.llvm.org/docs/UsersManual.html#precompiled-headers
# clang -g -fPIC -x c++-header src/Evaluator.hpp -o src/Evaluator.hpp.pch

echo "\n============================\nCode Modification\n"

./bin/cakelisp --execute \
			   test/CodeModification.cake || exit $?

echo "\n============================\nBuild options\n"

./bin/cakelisp \
	test/BuildOptions.cake || exit $?

echo "\n============================\nExecute\n"

./bin/cakelisp --execute \
	test/Execute.cake || exit $?

echo "\n============================\nHot reloadable library\n"

./bin/cakelisp \
	runtime/Config_Linux.cake runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake || exit $?

# TestMain is the loader. It doesn't care at all about fancy hot reloading macros, it just loads libs
# ./bin/cakelisp \
	# runtime/Config_Windows.cake runtime/HotLoader.cake  || exit $?

echo "\n============================\nHot loader\n"

./bin/cakelisp \
	runtime/Config_Linux.cake runtime/HotLoader.cake || exit $?

echo "\n============================\nCompile-time defines\n"

./bin/cakelisp --execute \
	test/Defines.cake || exit $?

# ./bin/cakelisp CrossCompile_Windows.cake || exit $?

echo "\n============================\nMulti-line strings\n"

./bin/cakelisp --execute test/MultiLineStrings.cake || exit $?

echo "\n============================\nBuild helpers\n"

./bin/cakelisp --execute --verbose-processes test/BuildHelpers.cake || exit $?

# echo "\nLink options\n"
# ./bin/cakelisp --verbose-processes test/LinkOptions.cake || exit $?
