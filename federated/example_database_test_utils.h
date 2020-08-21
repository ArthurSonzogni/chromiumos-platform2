// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_EXAMPLE_DATABASE_TEST_UTILS_H_
#define FEDERATED_EXAMPLE_DATABASE_TEST_UTILS_H_

namespace base {
class FilePath;
}  // namespace base

namespace federated {

// Creates database and tables for testing.
int CreateDatabaseForTesting(const base::FilePath& db_path);

}  // namespace federated

#endif  // FEDERATED_EXAMPLE_DATABASE_TEST_UTILS_H_
