//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef SHILL_MOCK_DIAGNOSTICS_REPORTER_H_
#define SHILL_MOCK_DIAGNOSTICS_REPORTER_H_

#include <gmock/gmock.h>

#include "shill/diagnostics_reporter.h"

namespace shill {

class MockDiagnosticsReporter : public DiagnosticsReporter {
 public:
  MockDiagnosticsReporter();
  ~MockDiagnosticsReporter() override;

  MOCK_METHOD0(OnConnectivityEvent, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDiagnosticsReporter);
};

}  // namespace shill

#endif  // SHILL_MOCK_DIAGNOSTICS_REPORTER_H_
