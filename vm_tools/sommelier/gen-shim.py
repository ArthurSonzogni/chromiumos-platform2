#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A C++ code generator of Wayland protocol shims."""

# pylint: enable=import-error
import os
import sys
from xml.etree import ElementTree

# pylint: disable=import-error
from jinja2 import Template


def CppTypeForWaylandEventType(xml_type_string, interface):
    """Generates the type for a wayland event argument."""
    if xml_type_string == "new_id" or xml_type_string == "object":
        return "struct wl_resource *"
    else:
        return CppTypeForWaylandType(xml_type_string, interface)


def CppTypeForWaylandType(xml_type_string, interface):
    """Generates the type for generic wayland type."""
    if xml_type_string == "int" or xml_type_string == "fd":
        return "int32_t"
    elif xml_type_string == "uint" or xml_type_string == "new_id":
        return "uint32_t"
    elif xml_type_string == "fixed":
        return "wl_fixed_t"
    elif xml_type_string == "string":
        return "const char *"
    elif xml_type_string == "object":
        return "struct %s *" % interface
    elif xml_type_string == "array":
        return "struct wl_array *"
    else:
        raise ValueError("Invalid Type conversion: %s" % xml_type_string)


def GetRequestReturnType(args):
    """Gets the return type of a Wayland request."""
    for arg in args:
        if arg.attrib["type"] == "new_id":
            if "interface" in arg.attrib:
                return "struct %s *" % arg.attrib["interface"]
            else:
                return "void *"
    return "void"


def RequestXmlToJinjaInput(request):
    """Parses a <request> element into dictionary form for use of jinja template."""
    method = {"name": request.attrib["name"], "args": [], "ret": ""}
    method["ret"] = GetRequestReturnType(request.findall("arg"))

    for arg in request.findall("arg"):
        if arg.attrib["type"] == "new_id":
            if not arg.attrib.get("interface"):
                method["args"].append(
                    {
                        "type": "const struct wl_interface *",
                        "name": "interface",
                    }
                )
                method["args"].append({"type": "uint32_t", "name": "version"})
        else:
            method["args"].append(
                {
                    "name": arg.attrib["name"],
                    "type": CppTypeForWaylandType(
                        arg.attrib["type"], arg.attrib.get("interface", "")
                    ),
                }
            )
    return method


def EventXmlToJinjaInput(event):
    """Parses an <event> element into dictionary for for use of jinja template."""
    return {
        "name": event.attrib["name"],
        "args": [
            {
                "name": arg.attrib["name"],
                "type": CppTypeForWaylandEventType(
                    arg.attrib["type"], arg.attrib.get("interface", "")
                ),
            }
            for arg in event.findall("arg")
        ],
    }


def InterfaceXmlToJinjaInput(interface):
    """Creates an interface dict for XML interface input."""
    interf = {
        "name": "".join(
            [i.capitalize() for i in interface.attrib["name"].split("_")]
        )
        + "Shim",
        "name_underscore": interface.attrib["name"],
        "methods": [
            RequestXmlToJinjaInput(i) for i in interface.findall("request")
        ],
        "events": [EventXmlToJinjaInput(i) for i in interface.findall("event")],
    }
    return interf


def GenerateShims(in_xml, out_directory):
    """Generates shims for Wayland Protocols."""
    with open(
        os.path.dirname(os.path.abspath(__file__))
        + "/gen/protocol-shim.h.jinja2",
        encoding="utf-8",
    ) as f:
        shim_template = Template(f.read())
    with open(
        os.path.dirname(os.path.abspath(__file__))
        + "/gen/protocol-shim.cc.jinja2",
        encoding="utf-8",
    ) as f:
        shim_impl_template = Template(f.read())
    with open(
        os.path.dirname(os.path.abspath(__file__))
        + "/gen/mock-protocol-shim.h.jinja2",
        encoding="utf-8",
    ) as f:
        mock_template = Template(f.read())

    tree = ElementTree.parse(in_xml)
    root = tree.getroot()

    filename = os.path.basename(in_xml).split(".")[0]

    # Because some protocol files don't have the protocol name == file name, we
    # have to infer the name from the file name instead (gtk-shell :eyes:)
    protocol = {
        "interfaces": [
            InterfaceXmlToJinjaInput(i) for i in root.findall("interface")
        ],
        "name_hyphen": filename,
        "name_underscore": filename.replace("-", "_"),
    }

    with open(
        out_directory + "/" + protocol["name_hyphen"] + "-shim.h",
        "w",
        encoding="utf-8",
    ) as f:
        f.write(shim_template.render(protocol=protocol))

    with open(
        out_directory + "/" + protocol["name_hyphen"] + "-shim.cc",
        "w",
        encoding="utf-8",
    ) as f:
        f.write(shim_impl_template.render(protocol=protocol))

    with open(
        out_directory + "/mock-" + protocol["name_hyphen"] + "-shim.h",
        "w",
        encoding="utf-8",
    ) as f:
        f.write(mock_template.render(protocol=protocol))


if __name__ == "__main__":
    source_xml = sys.argv[1]
    out_dir = sys.argv[2]

    GenerateShims(source_xml, out_dir)
