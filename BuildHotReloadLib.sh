#!/bin/sh

./bin/cakelisp --verbose-processes \
			   runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake \
	&& cd runtime && jam -j4 libGeneratedCakelisp.so
