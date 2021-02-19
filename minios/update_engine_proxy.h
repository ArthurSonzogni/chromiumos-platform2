// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_UPDATE_ENGINE_PROXY_H_
#define MINIOS_UPDATE_ENGINE_PROXY_H_

#include <memory>
#include <string>
#include <utility>

#include <base/memory/weak_ptr.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

class UpdateEngineProxy {
 public:
  UpdateEngineProxy(
      std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface> proxy)
      : update_engine_proxy_(std::move(proxy)),
        weak_ptr_factory_(this),
        delegate_(nullptr) {}
  virtual ~UpdateEngineProxy() = default;

  UpdateEngineProxy(const UpdateEngineProxy&) = delete;
  UpdateEngineProxy& operator=(const UpdateEngineProxy&) = delete;

  class UpdaterDelegate {
   public:
    virtual ~UpdaterDelegate() = default;
    virtual void OnProgressChanged(
        const update_engine::StatusResult& status) = 0;
  };

  // Set callbacks to get update engine status updates.
  void Init();

  void SetDelegate(UpdaterDelegate* delegate) { delegate_ = delegate; }

 private:
  // Called on receiving Update Engine's  'Status Update` signal.
  void OnStatusUpdateAdvancedSignal(
      const update_engine::StatusResult& status_result);
  // Called on connecting to Update Engine's  'Status Update` signal.
  void OnStatusUpdateAdvancedSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success);

  std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
      update_engine_proxy_;
  base::WeakPtrFactory<UpdateEngineProxy> weak_ptr_factory_;
  UpdaterDelegate* delegate_;
};

#endif  // MINIOS_UPDATE_ENGINE_PROXY_H_
