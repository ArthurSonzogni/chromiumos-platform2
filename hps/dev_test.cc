// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "hps/dev.h"
#include "hps/hps_reg.h"

using hps::DevInterface;

namespace {

static int const kRetry = 5;
static int const kBlockSizeBytes = 128;

// Fake that implements a DevInterface.
// Setting fails_ will fail a read or write, and then decrement the
// fail count so that multiple retries will succeed after a set count.
// The cmd and len for each read and write are saved.
class DevInterfaceFake : public DevInterface {
 public:
  DevInterfaceFake() : fails_(0), cmd_(0), len_(0), reads_(0), writes_(0) {}
  virtual ~DevInterfaceFake() {}
  bool Read(uint8_t cmd, uint8_t* data, size_t len) override {
    ++reads_;
    cmd_ = cmd;
    len_ = len;
    if (fails_ > 0) {
      --fails_;
      return false;
    }
    if (len == 2) {
      data[0] = data_[0];
      data[1] = data_[1];
    }
    return true;
  }
  bool Write(uint8_t cmd, const uint8_t* data, size_t len) override {
    ++writes_;
    cmd_ = cmd;
    len_ = len;
    if (fails_ > 0) {
      --fails_;
      return false;
    }
    if (len == 2) {
      data_[0] = data[0];
      data_[1] = data[1];
    }
    return true;
  }
  size_t BlockSizeBytes() override { return kBlockSizeBytes; }

  int fails_;        // If non-zero, fail the request and decrement this count.
  uint8_t cmd_;      // Command byte of request.
  size_t len_;       // Length of request.
  uint8_t data_[2];  // Data read or written.
  int reads_;        // Count of Read calls.
  int writes_;       // Count of Write calls.
};

class DevInterfaceTest : public testing::Test {
 protected:
  DevInterfaceFake dev_;
};

/*
 * Check that a ReadReg reads the correct data.
 */
TEST_F(DevInterfaceTest, ReadReg) {
  dev_.data_[0] = 0x12;
  dev_.data_[1] = 0x34;
  int d = dev_.ReadReg(hps::HpsReg::kMagic);
  EXPECT_EQ(d, 0x1234);
  EXPECT_EQ(dev_.len_, 2);
  EXPECT_EQ(dev_.cmd_, 0x80);
  dev_.data_[0] = 0x89;
  dev_.data_[1] = 0xAB;
  d = dev_.ReadReg(hps::HpsReg(32));
  EXPECT_EQ(d, 0x89AB);
  EXPECT_EQ(dev_.cmd_, 0x80 | 32);
  EXPECT_EQ(dev_.len_, 2);
  EXPECT_EQ(dev_.reads_, 2);
}

/*
 * Check that a WriteReg writes the correct data.
 */
TEST_F(DevInterfaceTest, WriteReg) {
  EXPECT_TRUE(dev_.WriteReg(hps::HpsReg::kMagic, 0x1234));
  EXPECT_EQ(dev_.data_[0], 0x12);
  EXPECT_EQ(dev_.data_[1], 0x34);
  EXPECT_EQ(dev_.len_, 2);
  EXPECT_EQ(dev_.cmd_, 0x80);
  EXPECT_TRUE(dev_.WriteReg(hps::HpsReg(32), 0x89AB));
  EXPECT_EQ(dev_.data_[0], 0x89);
  EXPECT_EQ(dev_.data_[1], 0xAB);
  EXPECT_EQ(dev_.cmd_, 0x80 | 32);
  EXPECT_EQ(dev_.len_, 2);
  EXPECT_EQ(dev_.writes_, 2);
}

/*
 * Verify that a Read will fail once the retries are exceeded.
 */
TEST_F(DevInterfaceTest, ReadFail) {
  dev_.fails_ = kRetry;
  int d = dev_.ReadReg(hps::HpsReg::kMagic);
  EXPECT_EQ(d, -1);
  EXPECT_EQ(dev_.reads_, kRetry);
}

/*
 * Verify that a Write will fail once the retries are exceeded.
 */
TEST_F(DevInterfaceTest, WriteFail) {
  dev_.fails_ = kRetry;
  EXPECT_FALSE(dev_.WriteReg(hps::HpsReg::kMagic, 0x1234));
  EXPECT_EQ(dev_.writes_, kRetry);
}

/*
 * Verify that a failed Read will be retried and succeed with
 * the second attempt.
 */
TEST_F(DevInterfaceTest, ReadRetry) {
  dev_.fails_ = 1;
  dev_.data_[0] = 0x12;
  dev_.data_[1] = 0x34;
  int d = dev_.ReadReg(hps::HpsReg::kMagic);
  EXPECT_EQ(d, 0x1234);
  // One failed read, one success.
  EXPECT_EQ(dev_.reads_, 2);
}

/*
 * Verify that a failed Write will be retried and succeed with
 * the second attempt.
 */
TEST_F(DevInterfaceTest, WriteRetry) {
  dev_.fails_ = 1;
  EXPECT_TRUE(dev_.WriteReg(hps::HpsReg::kMagic, 0x1234));
  // One failed write, one success.
  EXPECT_EQ(dev_.writes_, 2);
}

/*
 * Verify that the correct block size is selected.
 */
TEST_F(DevInterfaceTest, BlockSize) {
  EXPECT_EQ(dev_.BlockSizeBytes(), kBlockSizeBytes);
}

}  //  namespace
