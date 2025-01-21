// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_TRANSLATOR_H_
#define ODML_I18N_TRANSLATOR_H_

#include <memory>
#include <string>
#include <utility>

#include <base/containers/flat_map.h>
#include <base/memory/weak_ptr.h>

#include "odml/i18n/translate_api.h"
#include "odml/utils/dlc_client_helper.h"
#include "odml/utils/odml_shim_loader.h"

namespace i18n {

// Pairs of the BCP-47 language code like "en", "fr", "zh" etc.
struct LangPair {
  bool operator==(const LangPair&) const = default;
  std::string source;
  std::string target;

  template <typename H>
  friend H AbslHashValue(H h, const LangPair& lang_pair) {
    return H::combine(std::move(h), lang_pair.source, lang_pair.target);
  }
};

// TODO(b/391279922): Add sessions to handle cache reserving/releasing.
class Translator {
 public:
  Translator() = default;
  virtual ~Translator() = default;

  Translator(const Translator&) = delete;
  Translator& operator=(const Translator&) = delete;

  // Initialize the translator. This would waits for ODML shim being ready.
  virtual void Initialize(base::OnceCallback<void(bool)> callback) = 0;

  // Returns the availability of translator.
  virtual bool IsAvailable() const = 0;

  // Download the DLC of |lang_pair| (order doesn't matter) if not yet
  // downloaded.
  virtual void DownloadDlc(
      const LangPair& lang_pair,
      base::OnceCallback<void(bool)> callback,
      odml::DlcProgressCallback progress = base::NullCallback()) = 0;

  // Returns if the DLC of |lang_pair| has been downloaded.
  virtual bool IsDlcDownloaded(const LangPair& lang_pair) = 0;

  // Translate the |input_text| from |lang_pair|.source to |lang_pair|.target.
  // Returns std::nullopt on failure, otherwise, returns the translation.
  virtual std::optional<std::string> Translate(
      const LangPair& lang_pair, const std::string& input_text) = 0;
};

}  // namespace i18n

#endif  // ODML_I18N_TRANSLATOR_H_
