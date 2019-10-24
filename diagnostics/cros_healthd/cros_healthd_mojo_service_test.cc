// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/edk/embedder/embedder.h>

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"
#include "mojo/cros_healthd.mojom.h"

using testing::ByRef;
using testing::ElementsAreArray;
using testing::Return;
using testing::StrictMock;

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Tests for the CrosHealthddMojoService class.
class CrosHealthdMojoServiceTest : public testing::Test {
 protected:
  CrosHealthdMojoServiceTest() {
    mojo::edk::Init();
    service_ = std::make_unique<CrosHealthdMojoService>(
        mojo::MakeRequest(&service_ptr_).PassMessagePipe());
  }

 private:
  base::MessageLoop message_loop_;
  mojo_ipc::CrosHealthdServicePtr service_ptr_;
  std::unique_ptr<CrosHealthdMojoService> service_;
};

}  // namespace diagnostics
