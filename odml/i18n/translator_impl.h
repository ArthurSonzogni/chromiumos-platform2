// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_TRANSLATOR_IMPL_H_
#define ODML_I18N_TRANSLATOR_IMPL_H_

#include <memory>
#include <string>
#include <utility>

#include "odml/i18n/translate_api.h"
#include "odml/i18n/translator.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"

namespace i18n {

class TranslatorImpl : public Translator {
 public:
  explicit TranslatorImpl(raw_ref<odml::OdmlShimLoader> shim_loader);
  ~TranslatorImpl() = default;

  TranslatorImpl(const TranslatorImpl&) = delete;
  TranslatorImpl& operator=(const TranslatorImpl&) = delete;

  void Initialize(base::OnceCallback<void(bool)> callback) override;
  bool IsAvailable() const override;
  void DownloadDlc(
      const LangPair& lang_pair,
      base::OnceCallback<void(bool)> callback,
      odml::DlcProgressCallback progress = base::NullCallback()) override;
  bool IsDlcDownloaded(const LangPair& lang_pair) override;
  std::optional<std::string> Translate(const LangPair& lang_pair,
                                       const std::string& input_text) override;
};

}  // namespace i18n

#endif  // ODML_I18N_TRANSLATOR_IMPL_H_
