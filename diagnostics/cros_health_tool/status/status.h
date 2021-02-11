// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_STATUS_STATUS_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_STATUS_STATUS_H_

namespace diagnostics {

// 'status' sub-command for cros-health-tool:
//
// Utility that queries the status of the cros_healthd daemon and external mojo
// remotes.
int status_main(int argc, char** argv);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_STATUS_STATUS_H_
