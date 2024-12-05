// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINT_TOOLS_IPP_IN_JSON_H_
#define PRINT_TOOLS_IPP_IN_JSON_H_

#include <string>
#include <vector>

#include <chromeos/libipp/frame.h>
#include <chromeos/libipp/parser.h>

// This function build JSON representation of the given IPP response along with
// the log from parsing it. When `compressed_json` is true, produced JSON
// content contains no unnecessary whitespaces what makes it as short as
// possible. When `compressed_json` is false, produced JSON is formatted to
// maximize readability.  If `filter` is non-empty, only attributes containing
// `filter` will be emitted.
bool ConvertToJson(const ipp::Frame& response,
                   const ipp::SimpleParserLog& log,
                   const std::string& filter,
                   bool compressed_json,
                   std::string* json);

// This function builds a compact JSON representation intended for human
// reading from `response` and `log`.  To make the output simpler, only the
// printer-attributes group is returned and the types of values are omitted.  If
// `filter` is non-empty, only attributes containing `filter` will be emitted.
// If a full-fidelity representation of `response` is needed, use
// `ConvertToJson` instead.
bool ConvertToSimpleJson(const ipp::Frame& response,
                         const ipp::SimpleParserLog& log,
                         const std::string& filter,
                         std::string* json);

#endif  //  PRINT_TOOLS_IPP_IN_JSON_H_
