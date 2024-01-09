# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Transforms power_manager prefs/defaults into enforceable schema."""

import os
import re


THIS_DIR = os.path.dirname(__file__)

PREF_DEF_FILE = os.path.join(
    THIS_DIR, "../../power_manager/common/power_constants.cc"
)
PREF_DEFAULTS_DIR = os.path.join(THIS_DIR, "../../power_manager/default_prefs")


def generate_yaml() -> str:
    """Transforms power_manager prefs/defaults into jsonschema."""
    result_lines = [
        """powerd_prefs_default: &powerd_prefs_default
  description: >-
    For details, see
    https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/power_manager/
  type: string

powerd_prefs: &powerd_prefs"""
    ]

    with open(PREF_DEF_FILE, "r", encoding="utf-8") as defs_stream:
        defs_content = defs_stream.read()
        prefs = re.findall(
            r'const char .*Pref.. =[ |\n] *"(.*)";', defs_content, re.MULTILINE
        )
        for pref in prefs:
            default_pref_path = os.path.join(PREF_DEFAULTS_DIR, pref)
            pref_name = pref.replace("_", "-")
            if os.path.exists(default_pref_path):
                result_lines.append("  %s:" % pref_name)
                result_lines.append("    <<: *powerd_prefs_default")
                with open(
                    default_pref_path, "r", encoding="utf-8"
                ) as default_stream:
                    default = default_stream.read()
                    result_lines.append(
                        '    default: "%s"' % default.strip().replace("\n", " ")
                    )
            else:
                result_lines.append("  %s: *powerd_prefs_default" % pref_name)

    return "".join(f"{x}\n" for x in result_lines)
