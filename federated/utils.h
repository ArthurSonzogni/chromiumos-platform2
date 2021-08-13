// Copyright 2020 The Chromium OS Authors. All rights reserved.
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

// Converts the mojom Example struct to a TensorFlow Example proto.
tensorflow::Example ConvertToTensorFlowExampleProto(
    const chromeos::federated::mojom::ExamplePtr& example);

}  // namespace federated

#endif  // FEDERATED_UTILS_H_
