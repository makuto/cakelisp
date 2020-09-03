#!/bin/sh

jam -j4 && ./cakelisp test/Basic.cake
# jam -j4 && ./src/runProcessTest
# jam -j4 && ./src/dynamicLoadTest
