// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_GET_COMMS_STATUS_COMMAND_H_
#define LIBEC_GET_COMMS_STATUS_COMMAND_H_

#include <brillo/brillo_export.h>

#include "libec/ec_command.h"

namespace ec {

class BRILLO_EXPORT GetCommsStatusCommand
    : public EcCommand<EmptyParam, struct ec_response_get_comms_status> {
 public:
  GetCommsStatusCommand();
  ~GetCommsStatusCommand() override = default;

  bool IsProcessing() const;
};

static_assert(!std::is_copy_constructible<GetCommsStatusCommand>::value,
              "EcCommands are not copyable by default");
static_assert(!std::is_copy_assignable<GetCommsStatusCommand>::value,
              "EcCommands are not copy-assignable by default");

}  // namespace ec

#endif  // LIBEC_GET_COMMS_STATUS_COMMAND_H_
