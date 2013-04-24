// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TEST_COMMON_H_
#define SHILL_TEST_COMMON_H_

#include "shill/refptr_types.h"

namespace shill {

class ServiceMockAdaptor;

// Test property change notifications that are implemented by all
// Services.
void TestCommonPropertyChanges(ServiceRefPtr service,
                               ServiceMockAdaptor *adaptor);
// Test AutoConnect property change notification. Implemented by
// all Services except EthernetService.
void TestAutoConnectPropertyChange(ServiceRefPtr service,
                                   ServiceMockAdaptor *adaptor);
// Test Name property change notification. Only VPNService allows
// changing the name property.
void TestNamePropertyChange(ServiceRefPtr service,
                           ServiceMockAdaptor *adaptor);

}  // namespace shill

#endif  // SHILL_TEST_COMMON_H_
