#!/bin/sh

jam -j4 || exit $?
./bin/cakelisp --verbose-processes --verbose-file-search \
			   runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake
