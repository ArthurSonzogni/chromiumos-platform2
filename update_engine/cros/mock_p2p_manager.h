// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_MOCK_P2P_MANAGER_H_
#define UPDATE_ENGINE_CROS_MOCK_P2P_MANAGER_H_

#include <string>

#include "update_engine/cros/fake_p2p_manager.h"

#include <gmock/gmock.h>

namespace chromeos_update_engine {

// A mocked, fake implementation of P2PManager.
class MockP2PManager : public P2PManager {
 public:
  MockP2PManager() {
    // Delegate all calls to the fake instance
    ON_CALL(*this, SetDevicePolicy(testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::SetDevicePolicy));
    ON_CALL(*this, IsP2PEnabled())
        .WillByDefault(testing::Invoke(&fake_, &FakeP2PManager::IsP2PEnabled));
    ON_CALL(*this, EnsureP2PRunning())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::EnsureP2PRunning));
    ON_CALL(*this, EnsureP2PNotRunning())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::EnsureP2PNotRunning));
    ON_CALL(*this, PerformHousekeeping())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::PerformHousekeeping));
    ON_CALL(*this,
            LookupUrlForFile(testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::LookupUrlForFile));
    ON_CALL(*this, FileShare(testing::_, testing::_))
        .WillByDefault(testing::Invoke(&fake_, &FakeP2PManager::FileShare));
    ON_CALL(*this, FileGetPath(testing::_))
        .WillByDefault(testing::Invoke(&fake_, &FakeP2PManager::FileGetPath));
    ON_CALL(*this, FileGetSize(testing::_))
        .WillByDefault(testing::Invoke(&fake_, &FakeP2PManager::FileGetSize));
    ON_CALL(*this, FileGetExpectedSize(testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::FileGetExpectedSize));
    ON_CALL(*this, FileGetVisible(testing::_, testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::FileGetVisible));
    ON_CALL(*this, FileMakeVisible(testing::_))
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::FileMakeVisible));
    ON_CALL(*this, CountSharedFiles())
        .WillByDefault(
            testing::Invoke(&fake_, &FakeP2PManager::CountSharedFiles));
  }
  MockP2PManager(const MockP2PManager&) = delete;
  MockP2PManager& operator=(const MockP2PManager&) = delete;

  ~MockP2PManager() override {}

  // P2PManager overrides.
  MOCK_METHOD1(SetDevicePolicy, void(const policy::DevicePolicy*));
  MOCK_METHOD0(IsP2PEnabled, bool());
  MOCK_METHOD0(EnsureP2PRunning, bool());
  MOCK_METHOD0(EnsureP2PNotRunning, bool());
  MOCK_METHOD0(PerformHousekeeping, bool());
  MOCK_METHOD4(
      LookupUrlForFile,
      void(const std::string&, size_t, base::TimeDelta, LookupCallback));
  MOCK_METHOD2(FileShare, bool(const std::string&, size_t));
  MOCK_METHOD1(FileGetPath, base::FilePath(const std::string&));
  MOCK_METHOD1(FileGetSize, ssize_t(const std::string&));
  MOCK_METHOD1(FileGetExpectedSize, ssize_t(const std::string&));
  MOCK_METHOD2(FileGetVisible, bool(const std::string&, bool*));
  MOCK_METHOD1(FileMakeVisible, bool(const std::string&));
  MOCK_METHOD0(CountSharedFiles, int());

  // Returns a reference to the underlying FakeP2PManager.
  FakeP2PManager& fake() { return fake_; }

 private:
  // The underlying FakeP2PManager.
  FakeP2PManager fake_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_MOCK_P2P_MANAGER_H_
