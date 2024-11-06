// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/simple_session.h"

#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/memory/ptr_util.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/mojom/on_device_model.mojom.h"

namespace coral {

SimpleSession::Ptr SimpleSession::New() {
  return base::WrapUnique(new SimpleSession());
}

mojo::PendingReceiver<on_device_model::mojom::Session>
SimpleSession::BindReceiver() {
  return session_.BindNewPipeAndPassReceiver();
}

void SimpleSession::Execute(on_device_model::mojom::InputOptionsPtr options,
                            base::OnceCallback<void(std::string)> callback) {
  // We only support 1 executing session at the time. Print a warning and
  // execute the incoming callback with empty string.
  if (callback_) {
    LOG(WARNING)
        << "Received another Execute during an ongoing Execute operation.";
    std::move(callback).Run("");
    return;
  }
  callback_ = std::move(callback);
  response_.clear();
  session_->Execute(std::move(options), receiver_.BindNewPipeAndPassRemote());
}

void SimpleSession::SizeInTokens(const std::string& text,
                                 base::OnceCallback<void(uint32_t)> callback) {
  session_->GetSizeInTokensDeprecated(text, std::move(callback));
}

void SimpleSession::OnResponse(on_device_model::mojom::ResponseChunkPtr chunk) {
  response_ += chunk->text;
}

void SimpleSession::OnComplete(
    on_device_model::mojom::ResponseSummaryPtr summary) {
  receiver_.reset();
  base::OnceCallback<void(std::string)> callback = std::move(callback_);
  std::string response = std::move(response_);
  // Don't use any member function or variable after this line, because the
  // SimpleSession may be destroyed inside the callback.

  std::move(callback).Run(std::move(response));
}

}  // namespace coral
