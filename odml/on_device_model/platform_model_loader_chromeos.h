// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_
#define ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_

#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <brillo/dbus/dbus_connection.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
// NOLINTNEXTLINE(build/include_alpha) "dbus-proxies.h" needs "dlcservice.pb.h"
#include <dlcservice-client/dlcservice/dbus-proxies.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo/public/cpp/bindings/remote.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/on_device_model/platform_model_loader.h"

namespace on_device_model {

class OnDeviceModelService;

class ChromeosPlatformModelLoader final : public PlatformModelLoader {
 public:
  ChromeosPlatformModelLoader(raw_ref<MetricsLibraryInterface> metrics,
                              raw_ref<OnDeviceModelService> service);
  ~ChromeosPlatformModelLoader() override;

  ChromeosPlatformModelLoader(const ChromeosPlatformModelLoader&) = delete;
  ChromeosPlatformModelLoader& operator=(const ChromeosPlatformModelLoader&) =
      delete;

  void LoadModelWithUuid(const base::Uuid& uuid,
                         mojo::PendingReceiver<mojom::OnDeviceModel> pending,
                         LoadModelCallback callback) override;

 private:
  // This object is returned as the result of DLC install success or failure.
  struct InstallResult {
    // The error associated with the install. |dlcservice::kErrorNone| indicates
    // a success. Any other error code, indicates a failure.
    std::string error;
    // The unique DLC ID which was requested to be installed.
    std::string dlc_id;
    // The path where the DLC is available for users to use.
    std::string root_path;
  };

  class PlatformModel final : public base::RefCounted<PlatformModel> {
   public:
    PlatformModel();

    std::string& version() { return version_; }
    mojo::Remote<mojom::OnDeviceModel>& cur_model() { return cur_model_; }
    mojo::Remote<mojom::OnDeviceModel>& base_model() { return base_model_; }

    base::WeakPtr<PlatformModel> AsWeakPtr() {
      return weak_ptr_factory_.GetWeakPtr();
    }

   private:
    friend class base::RefCounted<PlatformModel>;
    virtual ~PlatformModel();

    std::string version_;
    mojo::Remote<mojom::OnDeviceModel> cur_model_;
    mojo::Remote<mojom::OnDeviceModel> base_model_;
    base::WeakPtrFactory<PlatformModel> weak_ptr_factory_{this};
  };

  struct PlatformModelRefTraits {
    using PointerType = scoped_refptr<PlatformModel>;
    static bool IsNull(const PointerType& ptr);
    static mojom::OnDeviceModel* GetRawPointer(PointerType* ptr);
  };

  struct PendingLoad {
    PendingLoad(mojo::PendingReceiver<mojom::OnDeviceModel> p,
                LoadModelCallback c);
    PendingLoad(PendingLoad&&);
    ~PendingLoad();

    mojo::PendingReceiver<mojom::OnDeviceModel> pending;
    LoadModelCallback callback;
  };

  struct PlatformModelRecord {
    PlatformModelRecord();
    ~PlatformModelRecord();

    base::WeakPtr<PlatformModel> platform_model;
    std::vector<PendingLoad> pending_loads;
  };

  bool ReplyModelAlreadyLoaded(const base::Uuid& uuid);

  void ReplyError(const base::Uuid& uuid, mojom::LoadModelResult result);

  void GetDlcState(const std::string& dlc_id,
                   base::OnceCallback<void(const InstallResult&)> callback);

  void OnInstallDlcComplete(const base::Uuid& uuid,
                            const InstallResult& result);

  void LoadAdaptationPlatformModel(const base::Uuid& base_uuid,
                                   const std::string& base_version,
                                   const base::Uuid& uuid,
                                   const base::FilePath& dlc_root,
                                   const std::string& version,
                                   const std::string& weight_path,
                                   scoped_refptr<PlatformModel> model,
                                   mojom::LoadModelResult result);

  void FinishLoadModel(const base::Uuid& uuid,
                       const std::string& version,
                       scoped_refptr<PlatformModel> model,
                       mojom::LoadModelResult result);

  const raw_ref<MetricsLibraryInterface> metrics_;
  raw_ref<OnDeviceModelService> service_;
  mojo::ReceiverSetBase<
      mojo::Receiver<mojom::OnDeviceModel, PlatformModelRefTraits>,
      void>
      receivers_;
  std::map<base::Uuid, PlatformModelRecord> platform_models_;

  brillo::DBusConnection connection_;
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyInterface> dlc_proxy_;

  base::WeakPtrFactory<ChromeosPlatformModelLoader> weak_ptr_factory_{this};
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_
