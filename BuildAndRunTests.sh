#!/bin/sh

# jam -j4 && ./bin/cakelisp test/Macros.cake
# jam -j4 && ./bin/cakelisp test/Dependencies.cake
# jam -j4 && ./bin/cakelisp test/Basic.cake

# Build Cakelisp itself, generate the runtime, then build the runtime
# jam -j4 && ./bin/cakelisp --enable-hot-reloading runtime/TestMain.cake runtime/TextAdventure.cake \
	# && cd runtime && jam -j4

# jam -j4 && ./bin/cakelisp --ignore-cache --verbose-processes --verbose-build-process \
			   # runtime/TestMain.cake runtime/TextAdventure.cake

# TODO: Add precompiled headers to build system. They are a 60% speedup for compile-time building
# clang -g -fPIC -x c++-header src/Evaluator.hpp -o src/Evaluator.hpp.pch

# jam -j4 && ./bin/cakelisp --ignore-cache \
						  # runtime/TestMain.cake runtime/TextAdventure.cake

# jam -j4 && ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects --verbose-file-search \
						  # runtime/TestMain.cake runtime/TextAdventure.cake
# jam -j4 && ./bin/cakelisp \
			   # runtime/TestMain.cake runtime/TextAdventure.cake

# jam -j4 && ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects \
						  # test/CodeModification.cake

# jam -j4 && ./bin/cakelisp --ignore-cache --verbose-compile-time-build-objects \
						  # test/BuildOptions.cake
jam -j4 && ./bin/cakelisp --verbose-processes --execute \
						  test/Execute.cake
