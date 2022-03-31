// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/vtpm_service.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <brillo/dbus/dbus_method_response.h>

#include "vtpm/vtpm_interface.pb.h"

namespace vtpm {

void VtpmService::SendCommand(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<SendCommandResponse>>
        response,
    const SendCommandRequest& request) {
  VLOG(1) << __func__;
  // Currently this is null-implemented, and always returns an empty string.
  // TODO(b/227341806): Implement the commands to be supported.
  SendCommandResponse send_command_response;
  response->Return(send_command_response);
}
}  // namespace vtpm
