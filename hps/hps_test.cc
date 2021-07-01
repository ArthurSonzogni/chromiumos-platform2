// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "hps/hal/fake_dev.h"
#include "hps/hps.h"
#include "hps/hps_reg.h"

namespace {

class HPSTest : public testing::Test {
 protected:
  virtual void SetUp() {
    fake_ = hps::FakeDev::Create();
    hps_ = std::make_unique<hps::HPS>(fake_->CreateDevInterface());
  }
  void CreateBlob(const base::FilePath& file, int len) {
    base::File f(file, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    ASSERT_TRUE(f.IsValid());
    f.SetLength(len);
  }

  scoped_refptr<hps::FakeDev> fake_;
  std::unique_ptr<hps::HPS> hps_;
};

/*
 * Check for a magic number.
 */
TEST_F(HPSTest, MagicNumber) {
  EXPECT_EQ(hps_->Device()->ReadReg(hps::HpsReg::kMagic), hps::kHpsMagic);
}

/*
 * Check that features can be enabled/disabled, and
 * results are returned only when allowed.
 */
TEST_F(HPSTest, FeatureControl) {
  // No features enabled until module is ready.
  EXPECT_FALSE(hps_->Enable(0));
  EXPECT_FALSE(hps_->Enable(1));
  EXPECT_EQ(hps_->Result(0), -1);
  // Set the module to be ready for features.
  fake_->SkipBoot();
  EXPECT_FALSE(hps_->Enable(hps::kFeatures));
  EXPECT_FALSE(hps_->Disable(hps::kFeatures));
  EXPECT_EQ(hps_->Result(hps::kFeatures), -1);
  ASSERT_TRUE(hps_->Enable(0));
  ASSERT_TRUE(hps_->Enable(1));
  // Check that enabled features can be disabled.
  EXPECT_TRUE(hps_->Disable(0));
  EXPECT_TRUE(hps_->Disable(1));
  // Check that a result is returned if the feature is enabled.
  const int result = 42;
  fake_->SetF1Result(result);
  EXPECT_EQ(hps_->Result(0), -1);
  ASSERT_TRUE(hps_->Enable(0));
  EXPECT_EQ(hps_->Result(0), result);
  ASSERT_TRUE(hps_->Disable(0));
  EXPECT_EQ(hps_->Result(0), -1);
}

/*
 * Download testing.
 */
TEST_F(HPSTest, Download) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto f = temp_dir.GetPath().Append("blob");
  const int len = 1024;
  CreateBlob(f, len);
  // Download allowed to bank 0 in pre-booted state.
  ASSERT_TRUE(hps_->Download(0, f));
  // Make sure the right amount was written.
  EXPECT_EQ(fake_->GetBankLen(0), len);
  // Fail the memory write and confirm that the request fails.
  // TODO(amcrae): Refactor to use enum directly.
  fake_->Set(hps::FakeDev::Flags::kMemFail);
  ASSERT_FALSE(hps_->Download(0, f));
  // No change to length.
  EXPECT_EQ(fake_->GetBankLen(0), len);
  fake_->Clear(hps::FakeDev::Flags::kMemFail);
  EXPECT_FALSE(hps_->Download(1, f));
  fake_->SkipBoot();
  // No downloads allowed when running.
  EXPECT_FALSE(hps_->Download(0, f));
}

/*
 * Download testing with small block size
 */
TEST_F(HPSTest, DownloadSmallBlocks) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  auto f = temp_dir.GetPath().Append("blob");
  const int len = 1024;
  CreateBlob(f, len);
  fake_->SetBlockSizeBytes(32);
  // Download allowed to bank 0 in pre-booted state.
  ASSERT_TRUE(hps_->Download(0, f));
  // Make sure the right amount was written.
  EXPECT_EQ(fake_->GetBankLen(0), len);
}

/*
 * Boot testing.
 */
TEST_F(HPSTest, SkipBoot) {
  // Make sure features can't be enabled.
  ASSERT_FALSE(hps_->Enable(0));
  // Put the fake straight into application stage.
  fake_->SkipBoot();
  // Inform the HPS handler that application stage is ready.
  hps_->SkipBoot();
  // Ensure that features can be enabled.
  EXPECT_TRUE(hps_->Enable(0));
}

/*
 * Test normal boot, where the versions match and
 * the files are verified, so no flash update should occur.
 */
TEST_F(HPSTest, NormalBoot) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Create MCU and SPI flash filenames (but do not
  // create the files themselves).
  auto mcu = temp_dir.GetPath().Append("mcu");
  auto spi = temp_dir.GetPath().Append("spi");
  // Set the expected version
  const uint16_t version = 1234;
  fake_->SetVersion(version);
  // Set up the version and files.
  hps_->Init(version, mcu, spi);
  // Boot the module.
  ASSERT_TRUE(hps_->Boot());
  // Ensure that features can be enabled.
  EXPECT_TRUE(hps_->Enable(0));
  EXPECT_EQ(fake_->GetBankLen(0), 0);
  EXPECT_EQ(fake_->GetBankLen(1), 0);
}

/*
 * Test that the MCU flash is updated when not verified.
 */
TEST_F(HPSTest, McuUpdate) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Create MCU and SPI flash filenames (but do not
  // create the files themselves).
  auto mcu = temp_dir.GetPath().Append("mcu");
  auto spi = temp_dir.GetPath().Append("spi");
  const int len = 1024;
  CreateBlob(mcu, len);
  // Set the expected version
  const uint16_t version = 1234;
  fake_->SetVersion(version);
  fake_->Set(hps::FakeDev::Flags::kApplNotVerified);
  fake_->Set(hps::FakeDev::Flags::kResetApplVerification);
  // Set up the version and files.
  hps_->Init(version, mcu, spi);
  // Boot the module.
  ASSERT_TRUE(hps_->Boot());
  // Check that MCU was downloaded.
  EXPECT_EQ(fake_->GetBankLen(0), len);
  EXPECT_EQ(fake_->GetBankLen(1), 0);
}

/*
 * Test that the SPI flash is updated when not verified.
 */
TEST_F(HPSTest, SpiUpdate) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Create MCU and SPI flash filenames (but do not
  // create the files themselves).
  auto mcu = temp_dir.GetPath().Append("mcu");
  auto spi = temp_dir.GetPath().Append("spi");
  const int len = 1024;
  CreateBlob(spi, len);
  // Set the expected version
  const uint16_t version = 1234;
  fake_->SetVersion(version);
  fake_->Set(hps::FakeDev::Flags::kSpiNotVerified);
  fake_->Set(hps::FakeDev::Flags::kResetSpiVerification);
  // Set up the version and files.
  hps_->Init(version, mcu, spi);
  // Boot the module.
  ASSERT_TRUE(hps_->Boot());
  // Check that SPI was downloaded.
  EXPECT_EQ(fake_->GetBankLen(0), 0);
  EXPECT_EQ(fake_->GetBankLen(1), len);
}

/*
 * Test that the both SPI and MCU are updated
 * when not verified.
 */
TEST_F(HPSTest, BothUpdate) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Create MCU and SPI flash filenames (but do not
  // create the files themselves).
  auto mcu = temp_dir.GetPath().Append("mcu");
  auto spi = temp_dir.GetPath().Append("spi");
  const int len = 1024;
  CreateBlob(spi, len);
  CreateBlob(mcu, len);
  // Set the expected version
  const uint16_t version = 1234;
  fake_->SetVersion(version);
  fake_->Set(hps::FakeDev::Flags::kApplNotVerified);
  fake_->Set(hps::FakeDev::Flags::kResetApplVerification);
  fake_->Set(hps::FakeDev::Flags::kSpiNotVerified);
  fake_->Set(hps::FakeDev::Flags::kResetSpiVerification);
  // Set up the version and files.
  hps_->Init(version, mcu, spi);
  // Boot the module.
  ASSERT_TRUE(hps_->Boot());
  // Check that both MCU and SPI blobs were updated.
  EXPECT_EQ(fake_->GetBankLen(0), len);
  EXPECT_EQ(fake_->GetBankLen(1), len);
}

/*
 * Verify that mismatching version will update both MCU and SPI
 */
TEST_F(HPSTest, VersionUpdate) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Create MCU and SPI flash filenames (but do not
  // create the files themselves).
  auto mcu = temp_dir.GetPath().Append("mcu");
  auto spi = temp_dir.GetPath().Append("spi");
  const int len = 1024;
  CreateBlob(spi, len);
  CreateBlob(mcu, len);
  // Set the current version
  const uint16_t version = 1234;
  fake_->SetVersion(version);
  fake_->Set(hps::FakeDev::Flags::kSpiNotVerified);
  fake_->Set(hps::FakeDev::Flags::kResetSpiVerification);
  fake_->Set(hps::FakeDev::Flags::kIncrementVersion);
  // Set up the version to be the next version.
  hps_->Init(version + 1, mcu, spi);
  // Boot the module.
  ASSERT_TRUE(hps_->Boot());
  // Check that both MCU and SPI were downloaded.
  EXPECT_EQ(fake_->GetBankLen(0), len);
  EXPECT_EQ(fake_->GetBankLen(1), len);
}

}  //  namespace
