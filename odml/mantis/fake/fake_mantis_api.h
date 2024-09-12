// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_FAKE_FAKE_MANTIS_API_H_
#define ODML_MANTIS_FAKE_FAKE_MANTIS_API_H_

#include "odml/mantis/processor.h"

namespace mantis::fake {

const MantisAPI* GetMantisApi();

}  // namespace mantis::fake

#endif  // ODML_MANTIS_FAKE_FAKE_MANTIS_API_H_
