// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_MOCK_TRANSLATOR_H_
#define ODML_I18N_MOCK_TRANSLATOR_H_

#include <string>

#include "odml/i18n/translator.h"

namespace i18n {

class MockTranslator : public Translator {
 public:
  MOCK_METHOD(void,
              Initialize,
              (base::OnceCallback<void(bool)> callback),
              (override));
  MOCK_METHOD(bool, IsAvailable, (), (const override));
  MOCK_METHOD(void,
              DownloadDlc,
              (const LangPair& lang_pair,
               base::OnceCallback<void(bool)> callback,
               odml::DlcProgressCallback progress),
              (override));
  MOCK_METHOD(bool, IsDlcDownloaded, (const LangPair& lang_pair), (override));
  MOCK_METHOD(void,
              Translate,
              (const LangPair& lang_pair,
               const std::string& input_text,
               base::OnceCallback<void(std::optional<std::string>)> callback),
              (override));
  MOCK_METHOD(std::optional<std::string>,
              TranslateSync,
              (const LangPair& lang_pair, const std::string& input_text),
              (override));
};

}  // namespace i18n

#endif  // ODML_I18N_MOCK_TRANSLATOR_H_
