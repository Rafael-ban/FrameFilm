#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=$(mktemp /tmp/framefilm-ark-sleep-test.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  -I"$here/stubs" \
  -I"$here/../../components/film_service/inc" \
  "$here/test_service_monitor.c" -o "$bin"
"$bin"
