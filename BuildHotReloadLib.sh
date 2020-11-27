#!/bin/sh

./bin/cakelisp runtime/HotReloadingCodeModifier.cake runtime/TextAdventure.cake \
	&& cd runtime && jam -j4 libGeneratedCakelisp.so
