// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/mock_example_database.h"

namespace federated {

MockExampleDatabase::MockExampleDatabase(const base::FilePath& db_path)
    : ExampleDatabase(db_path, {}) {}

}  // namespace federated
