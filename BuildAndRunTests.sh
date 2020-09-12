#!/bin/sh

jam -j4 && ./bin/cakelisp test/Dependencies.cake
# jam -j4 && ./bin/cakelisp test/Basic.cake
