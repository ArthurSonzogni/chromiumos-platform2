// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_LIFELINE_FD_SERVICE_H_
#define PATCHPANEL_MOCK_LIFELINE_FD_SERVICE_H_

#include "patchpanel/lifeline_fd_service.h"

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <gmock/gmock.h>

namespace patchpanel {

class MockLifelineFDService : public LifelineFDService {
 public:
  MockLifelineFDService();
  explicit MockLifelineFDService(const MockLifelineFDService&) = delete;
  MockLifelineFDService& operator=(const MockLifelineFDService&) = delete;
  virtual ~MockLifelineFDService();

  MOCK_METHOD(base::ScopedClosureRunner,
              AddLifelineFD,
              (base::ScopedFD, base::OnceClosure),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_LIFELINE_FD_SERVICE_H_
