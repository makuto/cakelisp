#!/bin/sh

jam -j4 || exit $?
./bin/cakelisp \
			   runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake
