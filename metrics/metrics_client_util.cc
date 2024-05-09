// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/metrics_client_util.h"

#include <stdio.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <base/strings/string_number_conversions.h>

namespace metrics_client {

void ShowUsage(FILE* err) {
  fprintf(
      err,
      "Usage:  metrics_client [-W <file>] [-n <num_samples>] [-t] name sample "
      "min max nbuckets\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -e   name sample "
      "max\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -s   name sample\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -v   event\n"
      "        metrics_client [-W <file>] [-n <num_samples>] -u action\n"
      "        metrics_client [-W <file>] -R <file>\n"
      "        metrics_client [-cCDg]\n"
      "        metrics_client --structured <project> <event> "
      "[--<field>=<value> ...]\n"
      "\n"
      "  default: send an integer-valued histogram sample\n"
      "           |min| > 0, |min| <= sample < |max|\n"
      "  -C: Create consent file such that -c will return 0.\n"
      "  -D: Delete consent file such that -c will return 1.\n"
      "  -R <file>: Replay events from a file and truncate it.\n"
      "  -W <file>: Write events to a file; append to it if it exists.\n"
      "  -c: return exit status 0 if user consents to stats, 1 otherwise,\n"
      "      in guest mode always return 1\n"
      "  -e: send linear/enumeration histogram data\n"
      "  -g: return exit status 0 if machine in guest mode, 1 otherwise\n"
      "  -n <num_samples>: Sends |num_samples| identical samples\n"
      // The -i flag prints the client ID, if it exists and is valid.
      // It is not advertised here because it is deprecated and for internal
      // use only (at least by the log tool in debugd).
      "  -s: send a sparse histogram sample\n"
      "  -t: convert sample from double seconds to int milliseconds\n"
      "  -u: send a user action\n"
      "  -v: send a Platform.CrOSEvent enum histogram sample\n"
      "  --structured: send a structure metrics event.\n");
}

std::optional<std::string> ParseStringStructuredMetricsArg(
    std::string_view arg) {
  return std::string(arg);
}

std::optional<int64_t> ParseIntStructuredMetricsArg(std::string_view arg) {
  int64_t result;
  if (base::StringToInt64(arg, &result)) {
    return result;
  }
  return std::nullopt;
}

std::optional<double> ParseDoubleStructuredMetricsArg(std::string_view arg) {
  double result;
  if (base::StringToDouble(arg, &result)) {
    return result;
  }
  return std::nullopt;
}

std::optional<std::vector<int64_t>> ParseIntArrayStructuredMetricsArg(
    std::string_view arg) {
  std::vector<int64_t> result;
  if (arg.empty()) {
    return result;
  }
  auto comma = arg.find(",");
  while (comma != std::string_view::npos) {
    int64_t next;
    if (!base::StringToInt64(arg.substr(0, comma), &next)) {
      return std::nullopt;
    }
    result.push_back(next);
    arg = arg.substr(comma + 1);
    comma = arg.find(",");
  }
  int64_t last;
  if (!base::StringToInt64(arg, &last)) {
    return std::nullopt;
  }
  result.push_back(last);
  return result;
}
}  // namespace metrics_client
