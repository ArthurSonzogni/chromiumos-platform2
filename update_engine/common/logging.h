// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

namespace chromeos_update_engine {

// Set up logging. |log_to_system| and |log_to_file| specifies
// the destination of logs.
void SetupLogging(bool log_to_system, bool log_to_file);

}  // namespace chromeos_update_engine
