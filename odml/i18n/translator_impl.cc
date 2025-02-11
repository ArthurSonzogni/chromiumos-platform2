// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/i18n/translator_impl.h"

#include <memory>

#include <base/containers/flat_map.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <ml_core/dlc/dlc_client.h>

#include "odml/i18n/translate_api.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"

namespace i18n {

namespace {

using TranslateAPIGetter = const TranslateAPI* (*)();

// If the shim is not ready, this function will retry the function with the
// given arguments after the shim is ready, and the ownership of callback &
// args will be taken in this kind of case.
// Returns true if the function will be retried.
template <typename FuncType,
          typename ObjectPtrType,
          typename CallbackType,
          typename FailureType,
          typename... Args>
bool RetryIfShimIsNotReady(FuncType func,
                           ObjectPtrType object_ptr,
                           const raw_ref<odml::OdmlShimLoader> shim_loader,
                           CallbackType& callback,
                           FailureType failure_result,
                           Args&... args) {
  if (shim_loader->IsShimReady()) {
    return false;
  }

  auto split = base::SplitOnceCallback(std::move(callback));
  base::OnceClosure retry_cb = base::BindOnce(
      func, object_ptr, std::move(args)..., std::move(split.first));

  shim_loader->EnsureShimReady(base::BindOnce(
      [](CallbackType callback, base::OnceClosure retry_cb,
         FailureType failure_result, bool result) {
        if (!result) {
          LOG(ERROR) << "Failed to ensure the shim is ready.";
          std::move(callback).Run(std::move(failure_result));
          return;
        }
        std::move(retry_cb).Run();
      },
      std::move(split.second), std::move(retry_cb), std::move(failure_result)));

  return true;
}

std::string LangPairString(const LangPair& lang_pair) {
  return absl::StrCat(lang_pair.target, "-", lang_pair.source);
}

std::string SortedLangPairString(const LangPair& lang_pair) {
  if (lang_pair.source < lang_pair.target) {
    return absl::StrCat(lang_pair.source, "-", lang_pair.target);
  }
  return absl::StrCat(lang_pair.target, "-", lang_pair.source);
}

std::string GetDlcName(const LangPair& lang_pair) {
  return absl::StrCat("translate-", SortedLangPairString(lang_pair));
}

}  // namespace

TranslatorImpl::TranslatorImpl(raw_ref<odml::OdmlShimLoader> shim_loader)
    : shim_loader_(shim_loader) {}

TranslatorImpl::~TranslatorImpl() {
  for (const auto& [lang_pair, dict_ptr] : dictionaries_) {
    api_->Destroy(dict_ptr);
  }
}

void TranslatorImpl::Initialize(base::OnceCallback<void(bool)> callback) {
  if (api_) {
    std::move(callback).Run(true);
    return;
  }
  if (RetryIfShimIsNotReady(&TranslatorImpl::Initialize,
                            weak_ptr_factory_.GetWeakPtr(), shim_loader_,
                            callback, false)) {
    return;
  }
  auto get_api = shim_loader_->Get<TranslateAPIGetter>("GetTranslateAPI");
  if (!get_api) {
    LOG(WARNING) << "Failed to get TranslateAPIGetter.";
    std::move(callback).Run(false);
    return;
  }
  api_ = get_api();
  if (!api_) {
    LOG(WARNING) << "Failed to get translate API.";
    std::move(callback).Run(false);
    return;
  }
  std::move(callback).Run(true);
  return;
}

bool TranslatorImpl::IsAvailable() const {
  return api_ != nullptr;
}

void TranslatorImpl::DownloadDlc(const LangPair& lang_pair,
                                 base::OnceCallback<void(bool)> callback,
                                 odml::DlcProgressCallback progress) {
  std::string dlc_name = GetDlcName(lang_pair);
  std::shared_ptr<odml::DlcClientPtr> dlc_client = odml::CreateDlcClient(
      dlc_name,
      base::BindOnce(&TranslatorImpl::OnInstallDlcComplete,
                     weak_ptr_factory_.GetWeakPtr(), dlc_name,
                     std::move(callback)),
      std::move(progress));
  (*dlc_client)->InstallDlc();
}

bool TranslatorImpl::IsDlcDownloaded(const LangPair& lang_pair) {
  return dlc_paths_.count(GetDlcName(lang_pair));
}

std::optional<std::string> TranslatorImpl::Translate(
    const LangPair& lang_pair, const std::string& input_text) {
  if (!IsAvailable()) {
    LOG(ERROR) << "Translator is not available";
    return std::nullopt;
  }
  std::optional<DictionaryManagerPtr> opt_dict = GetDictionary(lang_pair);
  if (!opt_dict) {
    return std::nullopt;
  }
  DictionaryManagerPtr dict_ptr = opt_dict.value();
  TranslateResult result = api_->Translate(dict_ptr, input_text);
  if (result.status != TranslateStatus::kOk) {
    LOG(ERROR) << base::StringPrintf("Failed to translate (%s), status: %d",
                                     LangPairString(lang_pair).c_str(),
                                     static_cast<int>(result.status));
    return std::nullopt;
  }
  return result.translation;
}

void TranslatorImpl::OnInstallDlcComplete(
    const std::string& dlc_name,
    base::OnceCallback<void(bool)> callback,
    base::expected<base::FilePath, std::string> result) {
  if (!result.has_value()) {
    LOG(ERROR) << "Failed to install translator DLC: " << result.error();
    std::move(callback).Run(false);
    return;
  }
  dlc_paths_[dlc_name] = result.value().value();
  std::move(callback).Run(true);
}

std::optional<DictionaryManagerPtr> TranslatorImpl::GetDictionary(
    const LangPair& lang_pair) {
  if (!IsAvailable()) {
    LOG(ERROR) << "Translator is not available";
    return std::nullopt;
  }
  auto dict = dictionaries_.find(lang_pair);
  if (dict != dictionaries_.end()) {
    return dict->second;
  }
  std::string dlc_name = GetDlcName(lang_pair);
  auto dlc_path = dlc_paths_.find(dlc_name);
  if (dlc_path == dlc_paths_.end()) {
    LOG(ERROR) << base::StringPrintf("DLC %s doesn't exist", dlc_name.c_str());
    return std::nullopt;
  }

  InitializeResult component =
      api_->Initialize(dlc_path->second, lang_pair.source, lang_pair.target);
  if (component.status != TranslateStatus::kOk) {
    LOG(ERROR) << base::StringPrintf(
        "Failed to initialize dictionary %s, status: %d",
        LangPairString(lang_pair).c_str(), static_cast<int>(component.status));
    return std::nullopt;
  }
  dictionaries_.insert({lang_pair, component.dictionary});
  return component.dictionary;
}

}  // namespace i18n
