// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/i18n/translator_impl.h"

#include <memory>

#include <base/logging.h>

#include "odml/i18n/translate_api.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"

namespace i18n {

TranslatorImpl::TranslatorImpl(raw_ref<odml::OdmlShimLoader> shim_loader) {}

void TranslatorImpl::Initialize(base::OnceCallback<void(bool)> callback) {
  LOG(WARNING) << __func__ << ": Not implemented";
  std::move(callback).Run(false);
  return;
}

bool TranslatorImpl::IsAvailable() const {
  LOG(WARNING) << __func__ << ": Not implemented";
  return false;
}

void TranslatorImpl::DownloadDlc(const LangPair& lang_pair,
                                 base::OnceCallback<void(bool)> callback,
                                 odml::DlcProgressCallback progress) {
  LOG(WARNING) << __func__ << ": Not implemented";
  std::move(callback).Run(false);
}

bool TranslatorImpl::IsDlcDownloaded(const LangPair& lang_pair) {
  LOG(WARNING) << __func__ << ": Not implemented";
  return false;
}

std::optional<std::string> TranslatorImpl::Translate(
    const LangPair& lang_pair, const std::string& input_text) {
  LOG(WARNING) << __func__ << ": Not implemented";
  return std::nullopt;
}

}  // namespace i18n
