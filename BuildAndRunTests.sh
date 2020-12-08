#!/bin/sh

# TODO: Add precompiled headers to build system. They are a 60% speedup for compile-time building
# clang -g -fPIC -x c++-header src/Evaluator.hpp -o src/Evaluator.hpp.pch

# jam -j4 && ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects \
						  # test/CodeModification.cake

# jam -j4 && ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects \
	# test/BuildOptions.cake

# jam -j4 && ./bin/cakelisp --verbose-processes --execute \
	# test/Execute.cake

# jam -j4 && ./bin/cakelisp --verbose-build-process \
						  # runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake || exit $?

# TestMain is the loader. It doesn't care at all about fancy hot reloading macros, it just loads libs
jam -j4 && ./bin/cakelisp \
						  runtime/HotLoader.cake  || exit $?
