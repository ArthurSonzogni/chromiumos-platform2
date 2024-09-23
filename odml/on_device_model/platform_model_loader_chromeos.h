// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_
#define ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <base/files/file_path.h>
#include <base/functional/callback.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <base/uuid.h>
#include <base/values.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver_set.h>
#include <mojo/public/cpp/bindings/remote.h>

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

  void LoadTextSafetyModelWithUuid(
      const base::Uuid& uuid,
      mojo::PendingReceiver<mojom::TextSafetyModel> pending,
      mojo::PendingRemote<mojom::PlatformModelProgressObserver>
          progress_observer,
      LoadModelCallback callback) override;

  void GetModelState(const base::Uuid& uuid,
                     GetModelStateCallback callback) override;

 private:
  template <typename ReceiverType>
  class PlatformModel final
      : public base::RefCounted<PlatformModel<ReceiverType>> {
   public:
    PlatformModel() = default;

    std::string& version() { return version_; }
    mojo::Remote<ReceiverType>& cur_model() { return cur_model_; }
    mojo::Remote<mojom::OnDeviceModel>& base_model() { return base_model_; }
    mojo::Remote<mojom::TextSafetyModel>& ts_model() { return ts_model_; }

    base::WeakPtr<PlatformModel> AsWeakPtr() {
      return weak_ptr_factory_.GetWeakPtr();
    }

   private:
    friend class base::RefCounted<PlatformModel>;
    virtual ~PlatformModel() = default;

    std::string version_;
    mojo::Remote<ReceiverType> cur_model_;
    mojo::Remote<mojom::OnDeviceModel> base_model_;
    mojo::Remote<mojom::TextSafetyModel> ts_model_;
    base::WeakPtrFactory<PlatformModel> weak_ptr_factory_{this};
  };

  template <typename ReceiverType>
  struct PlatformModelRefTraits {
    using PointerType = scoped_refptr<PlatformModel<ReceiverType>>;
    static bool IsNull(const PointerType& ptr);
    static ReceiverType* GetRawPointer(PointerType* ptr);
  };

  struct PendingLoad {
    mojo::PendingReceiver<mojom::OnDeviceModel> pending;
    mojo::PendingReceiver<mojom::TextSafetyModel> ts_pending;
    mojo::Remote<mojom::PlatformModelProgressObserver> progress_observer;
    LoadModelCallback callback;
  };

  struct PlatformModelRecord {
    double progress = 0.0;
    base::WeakPtr<PlatformModel<mojom::OnDeviceModel>> platform_model;
    base::WeakPtr<PlatformModel<mojom::TextSafetyModel>> ts_platform_model;
    std::vector<PendingLoad> pending_loads;
    std::unique_ptr<mojom::PlatformModelProgressObserver> base_model_observer;
  };

  void LoadUuid(const base::Uuid& uuid);

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

  void LoadAdaptationPlatformModel(
      const base::Uuid& base_uuid,
      const std::string& base_version,
      const base::Uuid& uuid,
      const base::FilePath& dlc_root,
      const std::string& version,
      const std::string& weight_path,
      scoped_refptr<PlatformModel<mojom::OnDeviceModel>> model,
      mojom::LoadModelResult result);

  void LoadBasePlatformModel(
      const std::optional<base::Value::Dict>& model_dict,
      const base::Uuid& uuid,
      const base::FilePath& dlc_root,
      const std::string& version,
      const std::string& weight_path,
      scoped_refptr<PlatformModel<mojom::OnDeviceModel>> model,
      mojom::LoadModelResult result);

  void FinishLoadModel(const base::Uuid& uuid,
                       const std::string& version,
                       scoped_refptr<PlatformModel<mojom::OnDeviceModel>> model,
                       mojom::LoadModelResult result);

  void FinishLoadTsModel(
      const base::Uuid& uuid,
      const std::string& version,
      scoped_refptr<PlatformModel<mojom::TextSafetyModel>> ts_model,
      mojom::LoadModelResult result);

  const raw_ref<MetricsLibraryInterface> metrics_;
  raw_ref<OnDeviceModelService> service_;
  mojo::ReceiverSetBase<
      mojo::Receiver<mojom::OnDeviceModel,
                     PlatformModelRefTraits<mojom::OnDeviceModel>>,
      void>
      receivers_;
  mojo::ReceiverSetBase<
      mojo::Receiver<mojom::TextSafetyModel,
                     PlatformModelRefTraits<mojom::TextSafetyModel>>,
      void>
      ts_receivers_;
  std::map<base::Uuid, PlatformModelRecord> platform_models_;

  base::WeakPtrFactory<ChromeosPlatformModelLoader> weak_ptr_factory_{this};
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_PLATFORM_MODEL_LOADER_CHROMEOS_H_
