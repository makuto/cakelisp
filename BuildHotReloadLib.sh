#!/bin/sh

./bin/cakelisp --enable-hot-reloading runtime/TextAdventure.cake \
	&& cd runtime && jam -j4 libGeneratedCakelisp.so
