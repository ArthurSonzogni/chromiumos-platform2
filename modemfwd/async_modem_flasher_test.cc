// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/async_modem_flasher.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "modemfwd/error.h"
#include "modemfwd/mock_modem_flasher.h"

using ::testing::_;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

namespace modemfwd {

namespace {
constexpr char kErrorCode[] = "KikuriHiroiError";
constexpr char kErrorMessage[] = "Ran out of happiness juice";
}  // namespace

class AsyncModemFlasherTest : public ::testing::Test {
 public:
  AsyncModemFlasherTest() {
    auto flasher = std::make_unique<MockModemFlasher>();
    modem_flasher_ = flasher.get();

    async_flasher_ =
        base::MakeRefCounted<AsyncModemFlasher>(std::move(flasher));
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  MockModemFlasher* modem_flasher_;
  scoped_refptr<AsyncModemFlasher> async_flasher_;
};

TEST_F(AsyncModemFlasherTest, ShouldFlash) {
  EXPECT_CALL(*modem_flasher_, ShouldFlash(_, _)).WillRepeatedly(Return(true));

  base::RunLoop run_loop;
  async_flasher_->ShouldFlash(
      nullptr, base::BindOnce(
                   [](base::OnceClosure quit_closure, bool should_flash,
                      brillo::ErrorPtr error) {
                     EXPECT_TRUE(should_flash);
                     std::move(quit_closure).Run();
                   },
                   run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(AsyncModemFlasherTest, ShouldFlashError) {
  EXPECT_CALL(*modem_flasher_, ShouldFlash(_, _))
      .WillOnce(WithArg<1>([](brillo::ErrorPtr* error) {
        Error::AddTo(error, FROM_HERE, kErrorCode, kErrorMessage);
        return false;
      }));

  base::RunLoop run_loop;
  async_flasher_->ShouldFlash(
      nullptr, base::BindOnce(
                   [](base::OnceClosure quit_closure, bool should_flash,
                      brillo::ErrorPtr error) {
                     EXPECT_FALSE(should_flash);
                     EXPECT_NE(error, nullptr);
                     EXPECT_EQ(error->GetCode(), kErrorCode);
                     EXPECT_EQ(error->GetMessage(), kErrorMessage);
                     std::move(quit_closure).Run();
                   },
                   run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(AsyncModemFlasherTest, BuildFlashConfig) {
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(_, _, _))
      .WillOnce(Return(std::make_unique<FlashConfig>()));

  base::RunLoop run_loop;
  async_flasher_->BuildFlashConfig(
      nullptr, std::nullopt,
      base::BindOnce(
          [](base::OnceClosure quit_closure,
             std::unique_ptr<FlashConfig> flash_cfg, brillo::ErrorPtr error) {
            EXPECT_NE(flash_cfg, nullptr);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(AsyncModemFlasherTest, BuildFlashConfigError) {
  EXPECT_CALL(*modem_flasher_, BuildFlashConfig(_, _, _))
      .WillOnce(WithArg<2>([](brillo::ErrorPtr* error) {
        Error::AddTo(error, FROM_HERE, kErrorCode, kErrorMessage);
        return nullptr;
      }));

  base::RunLoop run_loop;
  async_flasher_->BuildFlashConfig(
      nullptr, std::nullopt,
      base::BindOnce(
          [](base::OnceClosure quit_closure,
             std::unique_ptr<FlashConfig> flash_cfg, brillo::ErrorPtr error) {
            EXPECT_EQ(flash_cfg, nullptr);
            EXPECT_NE(error, nullptr);
            EXPECT_EQ(error->GetCode(), kErrorCode);
            EXPECT_EQ(error->GetMessage(), kErrorMessage);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(AsyncModemFlasherTest, RunFlash) {
  static constexpr base::TimeDelta kDuration = base::Seconds(20);

  EXPECT_CALL(*modem_flasher_, RunFlash(_, _, _, _))
      .WillOnce(WithArgs<2, 3>(
          [](base::TimeDelta* out_duration, brillo::ErrorPtr* error) {
            *out_duration = kDuration;
            return true;
          }));

  base::RunLoop run_loop;
  async_flasher_->RunFlash(
      nullptr, std::make_unique<FlashConfig>(),
      base::BindOnce(
          [](base::OnceClosure quit_closure, bool success,
             base::TimeDelta duration, brillo::ErrorPtr error) {
            EXPECT_TRUE(success);
            EXPECT_EQ(duration, kDuration);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(AsyncModemFlasherTest, RunFlashError) {
  EXPECT_CALL(*modem_flasher_, RunFlash(_, _, _, _))
      .WillOnce(WithArgs<2, 3>(
          [](base::TimeDelta* out_duration, brillo::ErrorPtr* error) {
            Error::AddTo(error, FROM_HERE, kErrorCode, kErrorMessage);
            return false;
          }));

  base::RunLoop run_loop;
  async_flasher_->RunFlash(
      nullptr, std::make_unique<FlashConfig>(),
      base::BindOnce(
          [](base::OnceClosure quit_closure, bool success,
             base::TimeDelta duration, brillo::ErrorPtr error) {
            EXPECT_FALSE(success);
            EXPECT_NE(error, nullptr);
            EXPECT_EQ(error->GetCode(), kErrorCode);
            EXPECT_EQ(error->GetMessage(), kErrorMessage);
            std::move(quit_closure).Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

}  // namespace modemfwd
