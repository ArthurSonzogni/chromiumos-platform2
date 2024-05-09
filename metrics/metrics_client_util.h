// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_METRICS_CLIENT_UTIL_H_
#define METRICS_METRICS_CLIENT_UTIL_H_

#include <stdio.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace metrics_client {
// Prints a usage message to the indicated file (normally stderr).
void ShowUsage(FILE* err);

// Helpers for SendStructureMetric. Each parses a command line argument into the
// indicated type. Each returns std::nullopt if the argument cannot be parsed.

// Parses a string argument from the command line. Redundant but needed for the
// auto-generated code.
std::optional<std::string> ParseStringStructuredMetricsArg(
    std::string_view arg);

// Parses an integer argument from the command line.
std::optional<int64_t> ParseIntStructuredMetricsArg(std::string_view arg);

// Parses a double argument from the command line.
std::optional<double> ParseDoubleStructuredMetricsArg(std::string_view arg);

// Parses a comma-separated list of integers from the command line.
std::optional<std::vector<int64_t>> ParseIntArrayStructuredMetricsArg(
    std::string_view arg);
}  // namespace metrics_client
#endif  // METRICS_METRICS_CLIENT_UTIL_H_
