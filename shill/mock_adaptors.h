// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_ADAPTORS_H_
#define SHILL_MOCK_ADAPTORS_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/adaptor_interfaces.h"
#include "shill/error.h"
#include "shill/store/key_value_store.h"

namespace shill {

// This file contains the stub Adaptor classes which provide the default
// implementation for *AdaptorInterface, and their mock versions if needed (only
// provide the mock functions which the tests require). Constructors and
// destructors of the mock classes are defined in the cc file for the
// compilation performance consideration.

class DeviceStubAdaptor : public DeviceAdaptorInterface {
 public:
  const RpcIdentifier& GetRpcIdentifier() const override { return rpc_id_; }

  void EmitBoolChanged(const std::string& name, bool value) override {}
  void EmitUintChanged(const std::string& name, uint32_t value) override {}
  void EmitUint16Changed(const std::string& name, uint16_t value) override {}
  void EmitIntChanged(const std::string& name, int value) override {}
  void EmitStringChanged(const std::string& name,
                         const std::string& value) override {}
  void EmitStringmapChanged(const std::string& name,
                            const Stringmap& value) override {}
  void EmitStringmapsChanged(const std::string& name,
                             const Stringmaps& value) override {}
  void EmitStringsChanged(const std::string& name,
                          const Strings& value) override {}
  void EmitKeyValueStoreChanged(const std::string& name,
                                const KeyValueStore& value) override {}
  void EmitKeyValueStoresChanged(const std::string& name,
                                 const KeyValueStores& value) override {}
  void EmitRpcIdentifierChanged(const std::string& name,
                                const RpcIdentifier& value) override {}
  void EmitRpcIdentifierArrayChanged(const std::string& name,
                                     const RpcIdentifiers& value) override {}

 private:
  RpcIdentifier rpc_id_{"/device_rpc"};
};

class DeviceMockAdaptor : public DeviceStubAdaptor {
 public:
  DeviceMockAdaptor();
  ~DeviceMockAdaptor() override;

  MOCK_METHOD(void, EmitBoolChanged, (const std::string&, bool), (override));
  MOCK_METHOD(void,
              EmitStringChanged,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(void,
              EmitKeyValueStoresChanged,
              (const std::string&, const KeyValueStores&),
              (override));
  MOCK_METHOD(void,
              EmitRpcIdentifierArrayChanged,
              (const std::string&, const std::vector<RpcIdentifier>&),
              (override));
};

class IPConfigStubAdaptor : public IPConfigAdaptorInterface {
 public:
  const RpcIdentifier& GetRpcIdentifier() const override { return rpc_id_; }

  void EmitBoolChanged(const std::string& name, bool value) override {}
  void EmitUintChanged(const std::string& name, uint32_t value) override {}
  void EmitIntChanged(const std::string& name, int value) override {}
  void EmitStringChanged(const std::string& name,
                         const std::string& value) override {}
  void EmitStringsChanged(const std::string& name,
                          const std::vector<std::string>& value) override {}

 private:
  RpcIdentifier rpc_id_{"/ipconfig_rpc"};
};

class IPConfigMockAdaptor : public IPConfigStubAdaptor {
 public:
  IPConfigMockAdaptor();
  ~IPConfigMockAdaptor() override;

  MOCK_METHOD(void,
              EmitStringChanged,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(void,
              EmitStringsChanged,
              (const std::string&, const std::vector<std::string>&),
              (override));
};

class ManagerStubAdaptor : public ManagerAdaptorInterface {
 public:
  const RpcIdentifier& GetRpcIdentifier() const override { return rpc_id_; }

  void RegisterAsync(
      base::OnceCallback<void(bool)> completion_callback) override {}
  void EmitBoolChanged(const std::string& name, bool value) override {}
  void EmitUintChanged(const std::string& name, uint32_t value) override {}
  void EmitIntChanged(const std::string& name, int value) override {}
  void EmitStringChanged(const std::string& name,
                         const std::string& value) override {}
  void EmitStringsChanged(const std::string& name,
                          const std::vector<std::string>& value) override {}
  void EmitKeyValueStoreChanged(const std::string& name,
                                const KeyValueStore& value) override {}
  void EmitRpcIdentifierChanged(const std::string& name,
                                const RpcIdentifier& value) override {}
  void EmitRpcIdentifierArrayChanged(const std::string& name,
                                     const RpcIdentifiers& value) override {}

 private:
  RpcIdentifier rpc_id_{"/manager_rpc"};
};

class ManagerMockAdaptor : public ManagerStubAdaptor {
 public:
  ManagerMockAdaptor();
  ~ManagerMockAdaptor() override;

  MOCK_METHOD(void,
              EmitStringChanged,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(void,
              EmitRpcIdentifierChanged,
              (const std::string&, const RpcIdentifier&),
              (override));
  MOCK_METHOD(void,
              EmitRpcIdentifierArrayChanged,
              (const std::string&, const std::vector<RpcIdentifier>&),
              (override));
};

class ProfileStubAdaptor : public ProfileAdaptorInterface {
 public:
  const RpcIdentifier& GetRpcIdentifier() const override { return rpc_id_; }

  void EmitBoolChanged(const std::string& name, bool value) override {}
  void EmitUintChanged(const std::string& name, uint32_t value) override {}
  void EmitIntChanged(const std::string& name, int value) override {}
  void EmitStringChanged(const std::string& name,
                         const std::string& value) override {}

 private:
  RpcIdentifier rpc_id_{"/profile_rpc"};
};

class RpcTaskStubAdaptor : public RpcTaskAdaptorInterface {
 public:
  static constexpr char kRpcId[] = "/rpc_task_rpc";
  static constexpr char kRpcConnId[] = "/rpc_task_rpc_conn";

  RpcTaskStubAdaptor() = default;

  const RpcIdentifier& GetRpcIdentifier() const override { return rpc_id_; }
  const RpcIdentifier& GetRpcConnectionIdentifier() const override {
    return rpc_conn_id_;
  }

 private:
  const RpcIdentifier rpc_id_{kRpcId};
  const RpcIdentifier rpc_conn_id_{kRpcConnId};
};

class ServiceStubAdaptor : public ServiceAdaptorInterface {
 public:
  const RpcIdentifier& GetRpcIdentifier() const override { return rpc_id_; }

  void EmitBoolChanged(const std::string& name, bool value) override {}
  void EmitUint8Changed(const std::string& name, uint8_t value) override {}
  void EmitUint16Changed(const std::string& name, uint16_t value) override {}
  void EmitUint16sChanged(const std::string& name,
                          const Uint16s& value) override {}
  void EmitUintChanged(const std::string& name, uint32_t value) override {}
  void EmitUint64Changed(const std::string& name, uint64_t value) override {}
  void EmitIntChanged(const std::string& name, int value) override {}
  void EmitRpcIdentifierChanged(const std::string& name,
                                const RpcIdentifier& value) override {}
  void EmitStringChanged(const std::string& name,
                         const std::string& value) override {}
  void EmitStringmapChanged(const std::string& name,
                            const Stringmap& value) override {}
  void EmitStringmapsChanged(const std::string& name,
                             const Stringmaps& value) override {}

 private:
  RpcIdentifier rpc_id_{"/service_rpc"};
};

// These are the functions that a Service adaptor must support
class ServiceMockAdaptor : public ServiceStubAdaptor {
 public:
  ServiceMockAdaptor();
  ~ServiceMockAdaptor() override;

  MOCK_METHOD(void, EmitBoolChanged, (const std::string&, bool), (override));
  MOCK_METHOD(void,
              EmitUint8Changed,
              (const std::string&, uint8_t),
              (override));
  MOCK_METHOD(void,
              EmitUint16Changed,
              (const std::string&, uint16_t),
              (override));
  MOCK_METHOD(void,
              EmitUint16sChanged,
              (const std::string&, const Uint16s&),
              (override));
  MOCK_METHOD(void, EmitIntChanged, (const std::string&, int), (override));
  MOCK_METHOD(void,
              EmitRpcIdentifierChanged,
              (const std::string&, const RpcIdentifier&),
              (override));
  MOCK_METHOD(void,
              EmitStringChanged,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(void,
              EmitStringmapChanged,
              (const std::string&, const Stringmap&),
              (override));
  MOCK_METHOD(void,
              EmitStringmapsChanged,
              (const std::string&, const Stringmaps&),
              (override));
};

#ifndef DISABLE_VPN
class ThirdPartyVpnStubAdaptor : public ThirdPartyVpnAdaptorInterface {
 public:
  void EmitPacketReceived(const std::vector<uint8_t>& packet) override {}
  void EmitPlatformMessage(uint32_t message) override {}
};

class ThirdPartyVpnMockAdaptor : public ThirdPartyVpnStubAdaptor {
 public:
  ThirdPartyVpnMockAdaptor();
  ~ThirdPartyVpnMockAdaptor() override;

  MOCK_METHOD(void, EmitPlatformMessage, (uint32_t), (override));
};
#endif

}  // namespace shill

#endif  // SHILL_MOCK_ADAPTORS_H_
