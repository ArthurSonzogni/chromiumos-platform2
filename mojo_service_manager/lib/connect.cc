// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/lib/connect.h"

#include <base/notreached.h>

namespace chromeos::mojo_service_manager {

BRILLO_EXPORT mojo::PendingRemote<mojom::ServiceManager>
ConnectToMojoServiceManager() {
  NOTIMPLEMENTED();
  return mojo::PendingRemote<mojom::ServiceManager>{};
}

}  // namespace chromeos::mojo_service_manager
