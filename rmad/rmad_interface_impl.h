// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_RMAD_INTERFACE_IMPL_H_
#define RMAD_RMAD_INTERFACE_IMPL_H_

#include "rmad/rmad_interface.h"

namespace rmad {

class RmadInterfaceImpl final : public RmadInterface {
 public:
  RmadInterfaceImpl() = default;
  RmadInterfaceImpl(const RmadInterfaceImpl&) = delete;
  RmadInterfaceImpl& operator=(const RmadInterfaceImpl&) = delete;

  ~RmadInterfaceImpl() override = default;

  void GetCurrentState(const GetCurrentStateRequest& request,
                       const GetCurrentStateCallback& callback) override;
};

}  // namespace rmad

#endif  // RMAD_RMAD_INTERFACE_IMPL_H_
