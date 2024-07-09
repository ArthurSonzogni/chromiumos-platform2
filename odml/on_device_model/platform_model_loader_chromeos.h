// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_
#define ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <base/uuid.h>
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

  void LoadModelWithUuid(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::OnDeviceModel> pending,
      mojo::PendingRemote<mojom::PlatformModelProgressObserver>
          progress_observer,
      LoadModelCallback callback) override;

  void GetModelState(const base::Uuid& uuid,
                     GetModelStateCallback callback) override;

 private:
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
                mojo::PendingRemote<mojom::PlatformModelProgressObserver> o,
                LoadModelCallback c);
    PendingLoad(PendingLoad&&);
    ~PendingLoad();

    mojo::PendingReceiver<mojom::OnDeviceModel> pending;
    mojo::Remote<mojom::PlatformModelProgressObserver> progress_observer;
    LoadModelCallback callback;
  };

  struct PlatformModelRecord {
    PlatformModelRecord();
    ~PlatformModelRecord();

    base::WeakPtr<PlatformModel> platform_model;
    std::vector<PendingLoad> pending_loads;
    std::unique_ptr<mojom::PlatformModelProgressObserver> base_model_observer;
  };

  bool ReplyModelAlreadyLoaded(const base::Uuid& uuid);

  void ReplyError(const base::Uuid& uuid, mojom::LoadModelResult result);

  void OnInstallDlcComplete(const base::Uuid& uuid,
                            base::expected<base::FilePath, std::string> result);

  void GetModelStateFromDlcState(
      const base::Uuid& uuid,
      GetModelStateCallback callback,
      base::expected<base::FilePath, std::string> result);

  void OnDlcProgress(const base::Uuid& uuid, double progress);

  void UpdateProgress(const base::Uuid& uuid, double progress);

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

  base::WeakPtrFactory<ChromeosPlatformModelLoader> weak_ptr_factory_{this};
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_
