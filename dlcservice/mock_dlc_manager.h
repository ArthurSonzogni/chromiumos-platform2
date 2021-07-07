// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_MOCK_DLC_MANAGER_H_
#define DLCSERVICE_MOCK_DLC_MANAGER_H_

#include "dlcservice/dlc_manager.h"

namespace dlcservice {

class MockDlcManager : public DlcManager {
 public:
  MockDlcManager() = default;
  MockDlcManager(const MockDlcManager&) = delete;
  MockDlcManager& operator=(const MockDlcManager&) = delete;

  MOCK_METHOD(DlcBase*,
              GetDlc,
              (const DlcId& id, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(void, Initialize, (), (override));
  MOCK_METHOD(DlcIdList, GetInstalled, (), (override));
  MOCK_METHOD(DlcIdList, GetExistingDlcs, (), (override));
  MOCK_METHOD(DlcIdList, GetDlcsToUpdate, (), (override));
  MOCK_METHOD(DlcIdList, GetSupported, (), (override));
  MOCK_METHOD(bool,
              InstallCompleted,
              (const DlcIdList& ids, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              UpdateCompleted,
              (const DlcIdList& ids, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              Install,
              (const DlcId& id,
               bool* external_install_needed,
               brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              FinishInstall,
              (const DlcId& id, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              CancelInstall,
              (const DlcId& id,
               const brillo::ErrorPtr& err_in,
               brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              Uninstall,
              (const DlcId& id, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              Purge,
              (const DlcId& id, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(void, ChangeProgress, (double progress), (override));
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_DLC_MANAGER_H_
