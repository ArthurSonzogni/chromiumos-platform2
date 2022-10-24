// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_UTILS_H_
#define FEDERATED_UTILS_H_

#include <string>

#include <base/files/file_path.h>

#include "federated/mojom/example.mojom.h"
#include "federated/protos/example.pb.h"
#include "federated/protos/feature.pb.h"

namespace federated {

// The maximum of example count that are consumed in one federated computation
// round.
extern const size_t kMaxStreamingExampleCount;
// The minimum of example count that are required in one federated computation
// round.
extern const size_t kMinExampleCount;
extern const char kSessionStartedState[];
extern const char kSessionStoppedState[];
extern const char kUserDatabasePath[];
extern const char kDatabaseFileName[];

// Gets the database file path with the given sanitized_username.
base::FilePath GetDatabasePath(const std::string& sanitized_username);

// Gets the base_dir inside the cryptohome.
// `base_dir` is used for opstats db which is created by brella library and
// serves as an on-device record of brella execution history and logs. Because
// the CrOS example storage is on cryptohome hence per-sanitized_username, the
// opstats db should also be like this.
base::FilePath GetBaseDir(const std::string& sanitized_username,
                          const std::string& client_name);

// Converts the mojom Example struct to a TensorFlow Example proto.
tensorflow::Example ConvertToTensorFlowExampleProto(
    const chromeos::federated::mojom::ExamplePtr& example);

}  // namespace federated

#endif  // FEDERATED_UTILS_H_
