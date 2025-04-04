// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_TRANSLATOR_IMPL_H_
#define ODML_I18N_TRANSLATOR_IMPL_H_

#include <memory>
#include <string>
#include <utility>

#include <absl/container/flat_hash_map.h>
#include <base/memory/weak_ptr.h>

#include "odml/i18n/translate_api.h"
#include "odml/i18n/translator.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"

namespace i18n {

class TranslatorImpl : public Translator {
 public:
  explicit TranslatorImpl(raw_ref<odml::OdmlShimLoader> shim_loader);
  ~TranslatorImpl() override;

  TranslatorImpl(const TranslatorImpl&) = delete;
  TranslatorImpl& operator=(const TranslatorImpl&) = delete;

  void Initialize(base::OnceCallback<void(bool)> callback) override;
  bool IsAvailable() const override;
  void DownloadDlc(
      const LangPair& lang_pair,
      base::OnceCallback<void(bool)> callback,
      odml::DlcProgressCallback progress = base::NullCallback()) override;
  bool IsDlcDownloaded(const LangPair& lang_pair) override;
  void Translate(
      const LangPair& lang_pair,
      const std::string& input_text,
      base::OnceCallback<void(std::optional<std::string>)> callback) override;
  std::optional<std::string> TranslateSync(
      const LangPair& lang_pair, const std::string& input_text) override;

 private:
  void DownloadDlcInternal(const LangPair& lang_pair,
                           base::OnceCallback<void(bool)> callback,
                           odml::DlcProgressCallback progress,
                           bool result);
  void TranslateInternal(
      const LangPair& lang_pair,
      const std::string& input_text,
      base::OnceCallback<void(std::optional<std::string>)> callback,
      bool result);
  void OnInstallDlcComplete(const std::string& dlc_name,
                            base::OnceCallback<void(bool)> callback,
                            base::expected<base::FilePath, std::string> result);
  std::optional<DictionaryManagerPtr> GetDictionary(const LangPair& lang_pair);

  const raw_ref<odml::OdmlShimLoader> shim_loader_;

  absl::flat_hash_map<LangPair, DictionaryManagerPtr> dictionaries_;

  absl::flat_hash_map<std::string, std::string> dlc_paths_;

  const TranslateAPI* api_ = nullptr;

  base::WeakPtrFactory<TranslatorImpl> weak_ptr_factory_{this};
};

}  // namespace i18n

#endif  // ODML_I18N_TRANSLATOR_IMPL_H_
