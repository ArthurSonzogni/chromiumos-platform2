// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACED_CLI_FACED_CLIENT_H_
#define FACED_FACED_CLI_FACED_CLIENT_H_

#include <absl/status/status.h>

namespace faced {

absl::Status ConnectAndDisconnectFromFaced();

}  // namespace faced

#endif  // FACED_FACED_CLI_FACED_CLIENT_H_
