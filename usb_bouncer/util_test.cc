// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_bouncer/util.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/test/bind.h>
#include <gtest/gtest.h>

#include "usb_bouncer/util_internal.h"

using brillo::SafeFD;

namespace usb_bouncer {

namespace {

constexpr char kSubdirName[] = "usb1";
constexpr char kNonUsbName[] = "domain0";
constexpr char kSysFSAuthorized[] = "authorized";
constexpr char kSysFSAuthorizedDefault[] = "authorized_default";
constexpr char kSysFSEnabled[] = "1";
constexpr char kSysFSDisabled[] = "0";

bool CreateDisabledFlag(const base::FilePath& flag) {
  if (!base::WriteFile(flag, kSysFSDisabled)) {
    LOG(ERROR) << "WriteFile('" << flag.value() << "', ...) failed.";
    return false;
  }
  return true;
}

bool CreateDeviceNode(const base::FilePath& dir) {
  if (!base::CreateDirectory(dir)) {
    LOG(ERROR) << "CreateDirectory('" << dir.value() << "') failed.";
    return false;
  }

  return CreateDisabledFlag(dir.Append(kSysFSAuthorized)) &&
         CreateDisabledFlag(dir.Append(kSysFSAuthorizedDefault));
}

bool CheckEnabledFlag(const base::FilePath& flag) {
  std::string flag_content;
  if (!base::ReadFileToString(flag, &flag_content)) {
    LOG(ERROR) << "ReadFileToString('" << flag.value() << "', ...) failed.";
    return false;
  }
  return flag_content == kSysFSEnabled;
}

bool CheckDeviceNodeAuthorized(const base::FilePath& dir) {
  return CheckEnabledFlag(dir.Append(kSysFSAuthorized)) &&
         CheckEnabledFlag(dir.Append(kSysFSAuthorizedDefault));
}

}  // namespace

constexpr size_t kTestMaxAttempts = 123;
constexpr uint32_t kTestDelayMicroseconds = 789;

// Do not actually sleep for tests.
int mock_usleep(useconds_t delay) {
  EXPECT_EQ(delay, kTestDelayMicroseconds);
  return 0;
}

TEST(UtilTest, WriteWithTimeout_Success) {
  SafeFD fd;
  SafeFD::Error err;
  std::tie(fd, err) = SafeFD::Root();
  ASSERT_EQ(err, SafeFD::Error::kNoError);

  base::TimeDelta delay = base::Microseconds(kTestDelayMicroseconds);
  std::string value = "test value";

  static size_t final_length = value.size();
  static bool ftruncate_called;
  ftruncate_called = false;
  EXPECT_TRUE(WriteWithTimeout(
      &fd, value, kTestMaxAttempts, delay,
      /* write_func */
      [](int fd, const void* data, size_t len) -> ssize_t {
        EXPECT_EQ(len, final_length);
        return len;
      },
      &mock_usleep,
      /* ftruncate_func */
      [](int fd, off_t len) -> int {
        EXPECT_EQ(len, final_length);
        ftruncate_called = true;
        return 0;
      }));
  EXPECT_TRUE(ftruncate_called);
}

TEST(UtilTest, WriteWithTimeout_IncrementalSuccess) {
  SafeFD fd;
  SafeFD::Error err;
  std::tie(fd, err) = SafeFD::Root();
  ASSERT_EQ(err, SafeFD::Error::kNoError);

  base::TimeDelta delay = base::Microseconds(kTestDelayMicroseconds);
  std::string value = "test value";

  static size_t final_length = value.size();
  static size_t expected_length;
  expected_length = value.size();
  static bool ftruncate_called;
  ftruncate_called = false;
  EXPECT_TRUE(WriteWithTimeout(
      &fd, value, kTestMaxAttempts, delay,
      /* write_func */
      [](int fd, const void* data, size_t len) -> ssize_t {
        EXPECT_EQ(expected_length, len);
        --expected_length;
        return 1;
      },
      &mock_usleep,
      /* ftruncate_func */
      [](int fd, off_t len) -> int {
        EXPECT_EQ(len, final_length);
        ftruncate_called = true;
        return 0;
      }));
  EXPECT_TRUE(ftruncate_called);
}

TEST(UtilTest, WriteWithTimeout_WriteError) {
  SafeFD fd;
  SafeFD::Error err;
  std::tie(fd, err) = SafeFD::Root();
  ASSERT_EQ(err, SafeFD::Error::kNoError);

  base::TimeDelta delay = base::Microseconds(kTestDelayMicroseconds);
  std::string value = "test value";

  static bool ftruncate_called;
  ftruncate_called = false;
  EXPECT_FALSE(WriteWithTimeout(
      &fd, value, kTestMaxAttempts, delay,
      /* write_func */
      [](int fd, const void* data, size_t len) -> ssize_t { return -1; },
      &mock_usleep,
      /* ftruncate_func */
      [](int fd, off_t len) -> int {
        ftruncate_called = true;
        return 0;
      }));
  EXPECT_FALSE(ftruncate_called);
}

TEST(UtilTest, WriteWithTimeout_TruncateError) {
  SafeFD fd;
  SafeFD::Error err;
  std::tie(fd, err) = SafeFD::Root();
  ASSERT_EQ(err, SafeFD::Error::kNoError);

  base::TimeDelta delay = base::Microseconds(kTestDelayMicroseconds);
  std::string value = "test value";

  static size_t final_length = value.size();
  static bool ftruncate_called;
  ftruncate_called = false;
  EXPECT_FALSE(WriteWithTimeout(
      &fd, value, kTestMaxAttempts, delay,
      /* write_func */
      [](int fd, const void* data, size_t len) -> ssize_t {
        EXPECT_EQ(len, final_length);
        return len;
      },
      &mock_usleep,
      /* ftruncate_func */
      [](int fd, off_t len) -> int {
        EXPECT_EQ(len, final_length);
        ftruncate_called = true;
        return -1;
      }));
  EXPECT_TRUE(ftruncate_called);
}

TEST(UtilTest, WriteWithTimeout_Timeout) {
  SafeFD fd;
  SafeFD::Error err;
  std::tie(fd, err) = SafeFD::Root();
  ASSERT_EQ(err, SafeFD::Error::kNoError);

  base::TimeDelta delay = base::Microseconds(kTestDelayMicroseconds);
  std::string value = "test value";

  static bool ftruncate_called;
  ftruncate_called = false;
  static size_t tries;
  tries = 0;
  ASSERT_FALSE(WriteWithTimeout(
      &fd, value, kTestMaxAttempts, delay,
      /* write_func */
      [](int fd, const void* data, size_t len) -> ssize_t {
        errno = EAGAIN;
        return -1;
      },
      /* ftruncate_func */
      [](useconds_t delay) -> int {
        ++tries;
        return 0;
      },
      /* ftruncate_func */
      [](int fd, off_t len) -> int {
        ftruncate_called = true;
        return 0;
      }));
  EXPECT_EQ(tries, kTestMaxAttempts);
  EXPECT_FALSE(ftruncate_called);
}

TEST(UtilTest, IncludeRuleAtLockscreen) {
  EXPECT_FALSE(IncludeRuleAtLockscreen(""));

  const std::string blocked_device =
      "allow id 0781:5588 serial \"12345678BF05\" name \"USB Extreme Pro\" "
      "hash \"9hMkYEMPjuNegGmzLIKwUp2MPctSL0tCWk7ruWGuOzc=\" with-interface "
      "08:06:50 with-connect-type \"unknown\"";
  EXPECT_FALSE(IncludeRuleAtLockscreen(blocked_device))
      << "Failed to filter: " << blocked_device;

  const std::string allowed_device =
      "allow id 0bda:8153 serial \"000001000000\" name \"USB 10/100/1000 LAN\" "
      "hash \"dljXy8thtljhoJo+O+hfhSlp1J89rz0Z4404iqKzakI=\" with-interface "
      "{ ff:ff:00 02:06:00 0a:00:00 0a:00:00 }";
  EXPECT_TRUE(IncludeRuleAtLockscreen(allowed_device))
      << "Failed to allow:" << allowed_device;
}

TEST(UtilTest, AuthorizeAll_Success) {
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()) << strerror(errno);
  ASSERT_TRUE(AuthorizeAll(temp_dir_.GetPath().value()));

  const base::FilePath subdir = temp_dir_.GetPath().Append(kSubdirName);
  ASSERT_TRUE(CreateDeviceNode(subdir))
      << "CreateDeviceNode('" << subdir.value() << "') failed.";
  const base::FilePath deepdir =
      temp_dir_.GetPath().Append(kSubdirName).Append(kSubdirName);
  ASSERT_TRUE(CreateDeviceNode(deepdir))
      << "CreateDeviceNode('" << deepdir.value() << "') failed.";

  const base::FilePath non_usb_dir = temp_dir_.GetPath().Append(kNonUsbName);
  ASSERT_TRUE(CreateDeviceNode(non_usb_dir))
      << "CreateDeviceNode('" << non_usb_dir.value() << "') failed.";
  const base::FilePath non_usb_deepdir =
      temp_dir_.GetPath().Append(kNonUsbName).Append(kNonUsbName);
  ASSERT_TRUE(CreateDeviceNode(non_usb_deepdir))
      << "CreateDeviceNode('" << non_usb_deepdir.value() << "') failed.";

  EXPECT_TRUE(AuthorizeAll(temp_dir_.GetPath().value()));
  EXPECT_TRUE(CheckDeviceNodeAuthorized(subdir));
  EXPECT_TRUE(CheckDeviceNodeAuthorized(deepdir));
  EXPECT_FALSE(CheckDeviceNodeAuthorized(non_usb_dir));
  EXPECT_FALSE(CheckDeviceNodeAuthorized(non_usb_deepdir));
}

TEST(UtilTest, AuthorizeAll_Failure) {
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()) << strerror(errno);

  const base::FilePath subdir = temp_dir_.GetPath().Append(kSubdirName);
  ASSERT_TRUE(CreateDeviceNode(subdir))
      << "CreateDeviceNode('" << subdir.value() << "') failed.";
  const base::FilePath deepdir =
      temp_dir_.GetPath().Append(kSubdirName).Append(kSubdirName);
  ASSERT_TRUE(CreateDeviceNode(deepdir))
      << "CreateDeviceNode('" << deepdir.value() << "') failed.";

  chmod(subdir.Append(kSysFSAuthorizedDefault).value().c_str(), 0000);

  EXPECT_FALSE(AuthorizeAll(temp_dir_.GetPath().value()));
  EXPECT_FALSE(CheckDeviceNodeAuthorized(subdir));
  EXPECT_TRUE(CheckDeviceNodeAuthorized(deepdir));
}

TEST(UtilTest, GetRuleFromString_Success) {
  EXPECT_TRUE(GetRuleFromString("allow id 0000:0000"));
}

TEST(UtilTest, GetRuleFromString_Failure) {
  EXPECT_FALSE(GetRuleFromString("aaff44"));
}

TEST(UtilTest, GetClassFromRule_EmptyInterfaces) {
  auto rule = GetRuleFromString("allow id 0000:0000");
  ASSERT_TRUE(rule);
  ASSERT_TRUE(rule.attributeWithInterface().empty());
  EXPECT_EQ(GetClassFromRule(rule), UMADeviceClass::kOther);
}

TEST(UtilTest, GetClassFromRule_SingleInterface) {
  auto rule = GetRuleFromString("allow with-interface 03:00:01");
  ASSERT_TRUE(rule);
  ASSERT_EQ(rule.attributeWithInterface().count(), 1);
  EXPECT_EQ(GetClassFromRule(rule), UMADeviceClass::kHID);
}

TEST(UtilTest, GetClassFromRule_MatchingInterfaces) {
  auto rule = GetRuleFromString("allow with-interface { 03:00:00 03:00:01 }");
  ASSERT_TRUE(rule);
  ASSERT_EQ(rule.attributeWithInterface().count(), 2);
  EXPECT_EQ(GetClassFromRule(rule), UMADeviceClass::kHID);
}

TEST(UtilTest, GetClassFromRule_DifferentInterfaces) {
  auto rule = GetRuleFromString("allow with-interface { 03:00:00 09:00:00 }");
  ASSERT_TRUE(rule);
  ASSERT_EQ(rule.attributeWithInterface().count(), 2);
  EXPECT_EQ(GetClassFromRule(rule), UMADeviceClass::kOther);
}

TEST(UtilTest, GetClassFromRule_AV) {
  auto rule =
      GetRuleFromString("allow with-interface { 01:00:00 0E:00:00 10:00:00 }");
  ASSERT_TRUE(rule);
  ASSERT_EQ(rule.attributeWithInterface().count(), 3);
  EXPECT_EQ(GetClassFromRule(rule), UMADeviceClass::kAV);
}

}  // namespace usb_bouncer
