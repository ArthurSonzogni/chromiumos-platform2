#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A build-time script to generate iptables-start scripts

This script is used in build time to generate /etc/patchpanel.iptables and
/etc/patchpanel/ip6tables.start script from a template together with the
constants defined in C++ code.
"""

import argparse
import json
from pathlib import Path

# pylint: disable=import-error
import jinja2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-t", "--template", required=True)
    parser.add_argument("-4", "--ipv4", action="store_true")
    parser.add_argument("-6", "--ipv6", action="store_true")
    parser.add_argument("-o", "--output")
    parser.add_argument("-i", "--input")
    args = parser.parse_args()

    template_str = Path(args.template).read_text(encoding="utf-8")
    template = jinja2.Template(template_str)

    data = {}
    if args.input:
        input_path = Path(args.input)
        with input_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    data["ipv4"] = args.ipv4
    data["ipv6"] = args.ipv6
    output = template.render(data)
    if args.output:
        output_path = Path(args.output)
        with output_path.open("w", encoding="utf-8") as f:
            f.write(output)
    else:
        print(output)


if __name__ == "__main__":
    main()
