// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MINIOS_INTERFACE_H_
#define MINIOS_MINIOS_INTERFACE_H_

#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <minios/proto_bindings/minios.pb.h>

namespace minios {

class MiniOsInterface {
 public:
  virtual ~MiniOsInterface() = default;

  virtual bool GetState(State* state_out, brillo::ErrorPtr* error) = 0;
};

}  // namespace minios

#endif  // MINIOS_MINIOS_INTERFACE_H_
