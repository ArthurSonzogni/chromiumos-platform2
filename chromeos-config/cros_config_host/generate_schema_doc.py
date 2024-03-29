# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates markdown from the JSON schema."""

import collections
import itertools
import os
import re
from typing import Any, Dict


def QuoteRegex(text):
    """Quote regex column.

    Use code block to quote the regex column, so most of the characters won't
    be treated as Markdown format syntax.  Pipe (|) is an exception, we need to
    explicitly escape it.

    Args:
        text: a string to quote

    Returns:
        A quoted string.
    """
    return "```%s```" % text.replace("|", "\\|")


def PopulateTypeDef(
    name, type_def, ref_types, output, inherited_build_only=False
):
    """Populates type definitions in the output (recursive)

    Args:
        name: Name of the type
        type_def: Dict containing all of the type def attributes
        ref_types: Shared type definitions using the #ref attribute
        output: Running array of markdown string output lines
        inherited_build_only: Boolean, whether this element is the child of a
          build-only element.
    """
    child_types = collections.OrderedDict()
    output.append("### %s" % name)
    output.append(
        "| Attribute | Type   | RegEx     | Required | Oneof Group "
        "| Build-only | Description |"
    )
    output.append(
        "| --------- | ------ | --------- | -------- | ----------- "
        "| ---------- | ----------- |"
    )

    attrs_by_group = {
        "": collections.OrderedDict(
            sorted(type_def.get("properties", {}).items())
        )
    }
    group_index = 0
    for group in itertools.chain(
        type_def.get("oneOf", []), type_def.get("anyOf", [])
    ):
        group_attrs = collections.OrderedDict(
            sorted(group.get("properties", {}).items())
        )
        group_name = "GROUP(%s)" % group_index
        for group_attr in group_attrs.values():
            match = re.match(r"\[(.*)\] .*", group_attr.get("description", ""))
            if match:
                group_name = match.group(1)
        attrs_by_group[group_name] = group_attrs
        group_index = group_index + 1

    additional_props = type_def.get("additionalProperties", False)
    if additional_props:
        output.append(
            "| [ANY] | N/A | N/A | N/A | N/A | N/A | "
            "This type allows additional properties not governed by the "
            "schema. "
            "See the type description for details on these additional "
            "properties.|"
        )

    for attr_group_name, attrs in attrs_by_group.items():
        for attr in attrs:
            attr_name = attr

            # pylint: disable=line-too-long
            # https://github.com/google/gitiles/blob/HEAD/Documentation/markdown.md#named-anchors
            attr_anchor = ""
            for c in attr_name:
                if c.isalnum():
                    attr_anchor += c
                elif c.isspace():
                    attr_anchor += "-"
                else:
                    attr_anchor += "_"
            attr_anchor = re.sub("-+", "-", attr_anchor)
            attr_anchor = re.sub("_+", "_", attr_anchor)

            type_attrs = attrs[attr]
            if "$ref" in type_attrs:
                type_attrs = ref_types[type_attrs["$ref"]]

            attr_type = type_attrs["type"]
            regex = type_attrs.get("pattern", "")
            if regex:
                # Regex need escaping for markdown
                regex = QuoteRegex(regex)
            description = type_attrs.get("description", "")
            description = description.replace("\n", " ")
            required_list = type_def.get("required", [])
            required = attr in required_list
            build_only = inherited_build_only or type_attrs.get(
                "build-only-element", False
            )
            if type_attrs["type"] == "object":
                child_types[attr_name] = type_attrs
                if build_only:
                    child_types[attr_name]["build-only-element"] = True
                attr_type = "[%s](#%s)" % (attr_name, attr_anchor)
            elif type_attrs["type"] == "array":
                description = type_attrs["items"].get("description", "")
                description = description.replace("\n", " ")
                if type_attrs["items"]["type"] == "object":
                    child_types[attr_name] = type_attrs["items"]
                    if build_only:
                        child_types[attr_name]["build-only-element"] = True
                    attr_type = "array - [%s](#%s)" % (attr_name, attr_anchor)
                else:
                    attr_type = "array - %s" % type_attrs["items"]["type"]
            elif type_attrs["type"] == "integer":
                if "minimum" in type_attrs:
                    description += " Minimum value: %s." % hex(
                        type_attrs["minimum"]
                    )
                if "maximum" in type_attrs:
                    description += " Maximum value: %s." % hex(
                        type_attrs["maximum"]
                    )

            output_tuple = (
                attr_name,
                attr_type,
                regex,
                required,
                attr_group_name,
                build_only,
                description,
            )
            output.append("| %s | %s | %s | %s | %s | %s | %s |" % output_tuple)

    output.append("")
    for child_type in child_types:
        child_is_build_only = inherited_build_only or child_types[
            child_type
        ].get("build-only-element", False)
        PopulateTypeDef(
            child_type,
            child_types[child_type],
            ref_types,
            output,
            inherited_build_only=child_is_build_only,
        )


def generate(schema: Dict[str, Any], output: "os.PathLike[str]") -> None:
    """Generates markdown documentation based on the JSON schema.

    Args:
        schema: Loaded schema.
        output: Output file.
    """
    ref_types = {}
    for type_def in schema.get("typeDefs", []):
        ref_types["#/typeDefs/%s" % type_def] = schema["typeDefs"][type_def]

    type_def_outputs = []
    type_def_outputs.append("[](begin_definitions)")
    type_def_outputs.append("")
    PopulateTypeDef(
        "model",
        schema["properties"]["chromeos"]["properties"]["configs"]["items"],
        ref_types,
        type_def_outputs,
    )
    type_def_outputs.append("")
    type_def_outputs.append("[](end_definitions)")
    type_def_outputs.append("")

    if output:
        pre_lines = []
        post_lines = []

        if os.path.isfile(output):
            with open(output, encoding="utf-8") as output_stream:
                output_lines = output_stream.readlines()
                pre_section = True
                post_section = False
                for line in output_lines:
                    if "begin_definitions" in line:
                        pre_section = False

                    if pre_section:
                        pre_lines.append(line)

                    if post_section:
                        post_lines.append(line)

                    if "end_definitions" in line:
                        post_section = True

        with open(output, "w", encoding="utf-8") as output_stream:
            if pre_lines:
                output_stream.writelines(pre_lines)

            output_stream.write("\n".join(type_def_outputs))

            if post_lines:
                output_stream.writelines(post_lines)
    else:
        print("\n".join(type_def_outputs))
