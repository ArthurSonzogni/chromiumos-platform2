// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera/diagnostics/camera_diagnostics_helpers.h"

#include <sstream>

namespace cros {

std::string DiagnosticsResultToJsonString(
    const cros::camera_diag::mojom::DiagnosticsResultPtr& result) {
  std::ostringstream oss;
  oss << "{";  // Begin JSON string
  oss << "\"suggested_issue\": " << result->suggested_issue;
  oss << ", \"num_analyzed_frames\": " << result->num_analyzed_frames;
  oss << ", \"analyzer_results\": [";  // Begin analyzer_results
  for (auto& analyzer_res : result->analyzer_results) {
    oss << "{\"type\": " << analyzer_res->type
        << ", \"status\": " << analyzer_res->status << "}, ";
  }
  oss << "]";  // End analyzer_results
  oss << "}";  // End JSON string
  return oss.str();
}

}  // namespace cros
