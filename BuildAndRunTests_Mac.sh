#!/bin/sh

# Make sure Cakelisp is up to date
. ./Build.sh || exit $?

./bin/cakelisp runtime/Config_MacOS.cake test/RunTests.cake || exit $?
