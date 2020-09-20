#!/bin/sh

# jam -j4 && ./bin/cakelisp test/Macros.cake
# jam -j4 && ./bin/cakelisp test/Dependencies.cake
# jam -j4 && ./bin/cakelisp test/Basic.cake

# Build Cakelisp itself, generate the runtime, build the runtime, then run the test
jam -j4 && ./bin/cakelisp runtime/TestMain.cake runtime/TextAdventure.cake \
	&& cd runtime && jam -j4 && ./HotReloadingTest
