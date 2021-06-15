// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_STATE_HANDLER_STATE_HANDLER_TEST_COMMON_H_
#define RMAD_STATE_HANDLER_STATE_HANDLER_TEST_COMMON_H_

#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "rmad/utils/json_store.h"

namespace rmad {

class StateHandlerTest : public testing::Test {
 protected:
  void SetUp() override;

  base::ScopedTempDir temp_dir_;
  scoped_refptr<JsonStore> json_store_;
};

}  // namespace rmad

#endif  // RMAD_STATE_HANDLER_STATE_HANDLER_TEST_COMMON_H_
