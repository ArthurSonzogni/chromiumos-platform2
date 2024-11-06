// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TITLE_GENERATION_SIMPLE_SESSION_H_
#define ODML_CORAL_TITLE_GENERATION_SIMPLE_SESSION_H_

#include <memory>
#include <string>

#include <base/functional/callback.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/mojom/on_device_model.mojom.h"

namespace coral {

// A simple session wrapper around mojom::Session and mojom::StreamingResponder
// that waits until the underlying execution to return the whole string
// response, and send it to the completion callback at once.
class SimpleSession : public on_device_model::mojom::StreamingResponder {
 public:
  using Ptr = std::unique_ptr<SimpleSession>;
  static Ptr New();

  ~SimpleSession() override = default;

  mojo::PendingReceiver<on_device_model::mojom::Session> BindReceiver();

  bool is_bound() const { return session_.is_bound(); }

  // This implementation doesn't support request queueing. The caller should
  // wait until the last Execute completes before sending another Execute.
  // Otherwise, the operation will do nothing and the `callback` argument will
  // be run with an empty string.
  void Execute(on_device_model::mojom::InputOptionsPtr options,
               base::OnceCallback<void(std::string)> callback);

  void SizeInTokens(const std::string& text,
                    base::OnceCallback<void(uint32_t)> callback);

 private:
  // on_device_model::mojom::StreamingResponder
  void OnResponse(on_device_model::mojom::ResponseChunkPtr chunk) override;
  void OnComplete(on_device_model::mojom::ResponseSummaryPtr summary) override;

  SimpleSession() = default;

  base::OnceCallback<void(std::string)> callback_;

  mojo::Receiver<on_device_model::mojom::StreamingResponder> receiver_{this};
  std::string response_;

  mojo::Remote<on_device_model::mojom::Session> session_;
};

}  // namespace coral

#endif  // ODML_CORAL_TITLE_GENERATION_SIMPLE_SESSION_H_
