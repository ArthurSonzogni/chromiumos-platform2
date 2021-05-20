// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/memory/ref_counted.h>
#include <gtest/gtest.h>

#include "hps/lib/fake_dev.h"
#include "hps/lib/hps.h"
#include "hps/lib/hps_reg.h"

namespace {

class HPSTest : public testing::Test {
 protected:
  virtual void SetUp() {
    fake_ = hps::FakeHps::Create();
    hps_ = std::make_unique<hps::HPS>(fake_->CreateDevInterface());
  }
  scoped_refptr<hps::FakeHps> fake_;
  std::unique_ptr<hps::HPS> hps_;
};

/*
 * Check for a magic number.
 */
TEST_F(HPSTest, MagicNumber) {
  EXPECT_EQ(hps_->Device()->ReadReg(hps::HpsReg::kMagic), hps::kHpsMagic);
}

}  //  namespace
