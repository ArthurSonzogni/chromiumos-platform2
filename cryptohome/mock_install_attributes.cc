// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/install_attributes_interface.h"
#include "cryptohome/mock_install_attributes.h"

namespace cryptohome {

MockInstallAttributes::MockInstallAttributes() : InstallAttributesInterface() {}
MockInstallAttributes::~MockInstallAttributes() {}

}  // namespace cryptohome
