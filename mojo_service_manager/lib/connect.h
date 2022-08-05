// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_LIB_CONNECT_H_
#define MOJO_SERVICE_MANAGER_LIB_CONNECT_H_

#include <brillo/brillo_export.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "mojo_service_manager/lib/mojom/service_manager.mojom.h"

namespace chromeos::mojo_service_manager {

BRILLO_EXPORT mojo::PendingRemote<mojom::ServiceManager>
ConnectToMojoServiceManager();

}  // namespace chromeos::mojo_service_manager

#endif  // MOJO_SERVICE_MANAGER_LIB_CONNECT_H_
