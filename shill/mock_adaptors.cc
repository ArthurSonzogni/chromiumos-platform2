// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_adaptors.h"

#include <string>

using std::string;

namespace shill {

// static
const char DeviceMockAdaptor::kRpcId[] = "/device_rpc";
// static
const char DeviceMockAdaptor::kRpcConnId[] = "/device_rpc_conn";

DeviceMockAdaptor::DeviceMockAdaptor()
    : rpc_id_(kRpcId),
      rpc_conn_id_(kRpcConnId) {
}

DeviceMockAdaptor::~DeviceMockAdaptor() {}

const string& DeviceMockAdaptor::GetRpcIdentifier() { return rpc_id_; }

const string& DeviceMockAdaptor::GetRpcConnectionIdentifier() {
  return rpc_conn_id_;
}

// static
const char IPConfigMockAdaptor::kRpcId[] = "/ipconfig_rpc";

IPConfigMockAdaptor::IPConfigMockAdaptor() : rpc_id_(kRpcId) {}

IPConfigMockAdaptor::~IPConfigMockAdaptor() {}

const string& IPConfigMockAdaptor::GetRpcIdentifier() { return rpc_id_; }

// static
const char ManagerMockAdaptor::kRpcId[] = "/manager_rpc";

ManagerMockAdaptor::ManagerMockAdaptor() : rpc_id_(kRpcId) {}

ManagerMockAdaptor::~ManagerMockAdaptor() {}

const string& ManagerMockAdaptor::GetRpcIdentifier() { return rpc_id_; }

// static
const char ProfileMockAdaptor::kRpcId[] = "/profile_rpc";

ProfileMockAdaptor::ProfileMockAdaptor() : rpc_id_(kRpcId) {}

ProfileMockAdaptor::~ProfileMockAdaptor() {}

const string& ProfileMockAdaptor::GetRpcIdentifier() { return rpc_id_; }

// static
const char RPCTaskMockAdaptor::kRpcId[] = "/rpc_task_rpc";
const char RPCTaskMockAdaptor::kRpcConnId[] = "/rpc_task_rpc_conn";

RPCTaskMockAdaptor::RPCTaskMockAdaptor()
    : rpc_id_(kRpcId),
      rpc_conn_id_(kRpcConnId) {}

RPCTaskMockAdaptor::~RPCTaskMockAdaptor() {}

const string& RPCTaskMockAdaptor::GetRpcIdentifier() { return rpc_id_; }
const string& RPCTaskMockAdaptor::GetRpcConnectionIdentifier() {
  return rpc_conn_id_;
}

// static
const char ServiceMockAdaptor::kRpcId[] = "/service_rpc";

ServiceMockAdaptor::ServiceMockAdaptor() : rpc_id_(kRpcId) {}

ServiceMockAdaptor::~ServiceMockAdaptor() {}

const string& ServiceMockAdaptor::GetRpcIdentifier() { return rpc_id_; }

#ifndef DISABLE_VPN
ThirdPartyVpnMockAdaptor::ThirdPartyVpnMockAdaptor() {}

ThirdPartyVpnMockAdaptor::~ThirdPartyVpnMockAdaptor() {}
#endif

}  // namespace shill
