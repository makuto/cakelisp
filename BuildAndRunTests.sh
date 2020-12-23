#!/bin/sh

# Make sure Cakelisp is up to date
. ./Build.sh || exit $?

# TODO: Add precompiled headers to build system. They are a 60% speedup for compile-time building
# See https://clang.llvm.org/docs/UsersManual.html#precompiled-headers
# clang -g -fPIC -x c++-header src/Evaluator.hpp -o src/Evaluator.hpp.pch

# ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects \
						  # test/CodeModification.cake

# ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects \
	# test/BuildOptions.cake

# ./bin/cakelisp --verbose-processes --execute \
	# test/Execute.cake

# ./bin/cakelisp --verbose-build-process \
						  # runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake || exit $?

# TestMain is the loader. It doesn't care at all about fancy hot reloading macros, it just loads libs
./bin/cakelisp --verbose-processes --verbose-include-scanning \
						  runtime/HotLoader.cake  || exit $?
