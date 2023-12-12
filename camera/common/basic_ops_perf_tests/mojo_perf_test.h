/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_BASIC_OPS_PERF_TESTS_MOJO_PERF_TEST_H_
#define CAMERA_COMMON_BASIC_OPS_PERF_TESTS_MOJO_PERF_TEST_H_

#include "camera/common/basic_ops_perf_tests/mojom/mojo_perf_test.mojom.h"

#include <cstdint>
#include <vector>

#include <mojo/public/cpp/bindings/receiver.h>

namespace cros::tests {

class MojoPerfTestImpl : public cros::mojom::MojoPerfTest {
 public:
  MojoPerfTestImpl(mojo::PendingReceiver<cros::mojom::MojoPerfTest> receiver,
                   base::OnceClosure disconnect_handler);
  void CallWithBuffer(const std::vector<uint8_t>&,
                      CallWithBufferCallback callback) override;

 private:
  mojo::Receiver<cros::mojom::MojoPerfTest> receiver_;
};

}  // namespace cros::tests

#endif  // CAMERA_COMMON_BASIC_OPS_PERF_TESTS_MOJO_PERF_TEST_H_
