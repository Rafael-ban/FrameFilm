#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=$(mktemp /tmp/framefilm-ark-image-test.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Wno-unused-function \
  -I"$here/stubs" \
  -I"$here/../../components/film_app/inc" \
  -I"$here/../../components/film_hal/inc" \
  -I"$here/../../components/film_service/inc" \
  -I"$here/../../components/film_sys/inc" \
  "$here/test_image_events.c" -o "$bin"
"$bin"
