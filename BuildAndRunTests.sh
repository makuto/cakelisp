#!/bin/sh

jam -j4 && ./bin/cakelisp test/Dependencies.cake
# jam -j4 && ./src/dependencyTest
# jam -j4 && ./src/runProcessTest
# jam -j4 && ./src/dynamicLoadTest
