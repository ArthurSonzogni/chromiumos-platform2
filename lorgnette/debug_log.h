// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_DEBUG_LOG_H_
#define LORGNETTE_DEBUG_LOG_H_

#include <base/files/file_path.h>

namespace lorgnette {

// If the file at `flagPath` exists, set up environment variables to put
// SANE backends into debug mode.  Returns true if debugging was enabled
// or false if not.
bool SetupDebugging(const base::FilePath& flagPath);

}  // namespace lorgnette

#endif  // LORGNETTE_DEBUG_LOG_H_
