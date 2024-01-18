#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""generate_early_boot_checks generates code to check early-boot feature state.

Feature_library requires all early-boot features to have default values
specified in a file, default_states.json. It enforces this by making the code to
check feature state private. This script uses default_states.json to generate
public code (one function for each feature) to actually check the feature state.
"""

import argparse
import dataclasses
import json
import sys
from typing import Dict

import jinja2  # pylint: disable=import-error


@dataclasses.dataclass
class Feature:
    """Wrapper class for parsed Feature state."""

    name: str
    default_val: bool
    params: Dict[str, str]


def generate_code(defaults_json: str, template_folder: str, header_path: str):
    loaded = json.loads(defaults_json)
    features = []
    for k, v in loaded.items():
        params = v["params"] if "params" in v else {}
        features.append(Feature(k, v["enabled_by_default"], params))

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(template_folder),
        autoescape=jinja2.select_autoescape(enabled_extensions=("cpp",)),
    )

    for feature in features:
        if not feature.name.startswith("CrOSEarlyBoot"):
            raise ValueError(f"{feature.name} must start with CrOSEarlyBoot")

    cc_template = env.get_template("early_boot_state_checks_cc.jinja")
    h_template = env.get_template("early_boot_state_checks_h.jinja")

    header_guard = header_path.upper().replace("/", "_").replace(".", "_")

    return (
        str(cc_template.render(features=features)),
        str(h_template.render(features=features, header_guard=header_guard)),
    )


def main(args):
    parser = argparse.ArgumentParser(
        description="Generates C++ functions to query individual feature status"
        " with the given default states.",
    )
    parser.add_argument(
        "--default_states",
        help="Path to JSON file containing default states.",
        required=True,
    )
    parser.add_argument(
        "--fileroot_output",
        help="Path to write output, with no extensions (e.g. .h).",
        required=True,
    )
    parser.add_argument(
        "--template_folder",
        help="Directory containing the jinja templates.",
        required=True,
    )
    # TODO(b/318555424): Support options to generate Rust and C code.

    opts = parser.parse_args(args=args)

    with open(opts.default_states, encoding="utf-8") as f:
        defaults_json = f.read()

    header_path = opts.fileroot_output + ".h"
    code, header = generate_code(
        defaults_json, opts.template_folder, header_path
    )

    with open(opts.fileroot_output + ".cc", "w", encoding="utf-8") as f:
        f.write(code)

    with open(header_path, "w", encoding="utf-8") as f:
        f.write(header)


if __name__ == "__main__":
    main(sys.argv[1:])
