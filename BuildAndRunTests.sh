#!/bin/sh

# jam -j4 && ./bin/cakelisp test/Macros.cake
# jam -j4 && ./bin/cakelisp test/Dependencies.cake
# jam -j4 && ./bin/cakelisp test/Basic.cake

# Build Cakelisp itself, generate the runtime, then build the runtime
jam -j4 && ./bin/cakelisp --enable-hot-reloading runtime/TestMain.cake runtime/TextAdventure.cake \
	&& cd runtime && jam -j4
