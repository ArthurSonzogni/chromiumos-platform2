// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_RMAD_INTERFACE_H_
#define RMAD_RMAD_INTERFACE_H_

#include <base/callback.h>
#include <rmad/proto_bindings/rmad.pb.h>

namespace rmad {

class RmadInterface {
 public:
  RmadInterface() = default;
  virtual ~RmadInterface() = default;

  using GetCurrentStateCallback =
      base::Callback<void(const GetCurrentStateReply&)>;
  virtual void GetCurrentState(const GetCurrentStateRequest& request,
                               const GetCurrentStateCallback& callback) = 0;
};

}  // namespace rmad

#endif  // RMAD_RMAD_INTERFACE_H_
