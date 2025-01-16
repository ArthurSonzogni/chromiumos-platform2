#!/bin/bash -eux
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Verifies FORTIFY diagnostics WAI for these tests. Making `gn` do this is
# relatively hacky, so a simple shell script is used instead.

my_dir="$(dirname "$(readlink -m "$0")")"
cd "${my_dir}"

# SC complains about CXX/CPPFLAGS/CXXFLAGS not being quoted below. That's
# intended.
# shellcheck disable=SC2206
verify_cmd=(
  # CXX should be exported by the ebuild; add'l flags are optional.
  ${CXX:?}
  ${CPPFLAGS:-}
  ${CXXFLAGS:-}
  # Put platform2's base on the include path.
  -I"${my_dir}/../.."
  # This tests GNU extensions
  -D_GNU_SOURCE
  # FORTIFY checks require optimizations to be enabled above -O0.
  -O2
  -Wformat-nonliteral
  # clang-fortify-tests has checks for getwd, which was removed in c++14.
  -std=c++11
  # All users will override -D_FORTIFY_SOURCE
  -U_FORTIFY_SOURCE
  # `-Xclang -verify` has Clang verify diagnostics that it emits internally.
  -Xclang
  -verify
  # -fsyntax-only has clnag skip everything after parsing.
  -fsyntax-only
  -DCOMPILATION_TESTS
  "${my_dir}/clang-fortify-tests.cpp"
)

"${verify_cmd[@]}" "-D_FORTIFY_SOURCE=1"
"${verify_cmd[@]}" "-D_FORTIFY_SOURCE=2"
