// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/events_handler.h"

#include <utility>

#include <base/bind.h>
#include <base/containers/contains.h>

#include "iioservice/daemon/common_types.h"
#include "iioservice/include/common.h"

namespace iioservice {

namespace {

cros::mojom::IioEventPtr ExtractIioEvent(iio_event_data event) {
  cros::mojom::IioEvent iio_event;
  uint64_t mask = event.id;

  return cros::mojom::IioEvent::New(
      ConvertChanType(
          static_cast<iio_chan_type>(IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(mask))),
      ConvertEventType(
          static_cast<iio_event_type>(IIO_EVENT_CODE_EXTRACT_TYPE(mask))),
      ConvertDirection(
          static_cast<iio_event_direction>(IIO_EVENT_CODE_EXTRACT_DIR(mask))),
      IIO_EVENT_CODE_EXTRACT_CHAN(mask), event.timestamp);
}

}  // namespace

// static
void EventsHandler::EventsHandlerDeleter(EventsHandler* handler) {
  if (handler == nullptr)
    return;

  if (!handler->event_task_runner_->BelongsToCurrentThread()) {
    handler->event_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&EventsHandler::EventsHandlerDeleter, handler));
    return;
  }

  delete handler;
}

// static
EventsHandler::ScopedEventsHandler EventsHandler::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> event_task_runner,
    libmems::IioDevice* iio_device) {
  ScopedEventsHandler handler(nullptr, EventsHandlerDeleter);

  iio_device->EnableAllEvents();

  handler.reset(new EventsHandler(std::move(ipc_task_runner),
                                  std::move(event_task_runner), iio_device));
  return handler;
}

EventsHandler::~EventsHandler() {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());

  for (ClientData* client : inactive_clients_) {
    if (client->events_observer.is_bound())
      client->events_observer.reset();
  }
  for (ClientData* client : active_clients_) {
    if (client->events_observer.is_bound())
      client->events_observer.reset();
  }
}

void EventsHandler::ResetWithReason(
    cros::mojom::SensorDeviceDisconnectReason reason, std::string description) {
  event_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&EventsHandler::ResetWithReasonOnThread,
                     weak_factory_.GetWeakPtr(), reason, description));
}

void EventsHandler::AddClient(
    ClientData* client_data,
    mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver>
        events_observer) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  event_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&EventsHandler::AddClientOnThread,
                                weak_factory_.GetWeakPtr(), client_data,
                                std::move(events_observer)));
}

void EventsHandler::RemoveClient(ClientData* client_data,
                                 base::OnceClosure callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  event_task_runner_->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(&EventsHandler::RemoveClientOnThread,
                     weak_factory_.GetWeakPtr(), client_data),
      std::move(callback));
}

void EventsHandler::UpdateEventsEnabled(
    ClientData* client_data,
    const std::vector<int32_t>& iio_event_indices,
    bool en,
    cros::mojom::SensorDevice::SetEventsEnabledCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  event_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&EventsHandler::UpdateEventsEnabledOnThread,
                                weak_factory_.GetWeakPtr(), client_data,
                                iio_event_indices, en, std::move(callback)));
}

void EventsHandler::GetEventsEnabled(
    ClientData* client_data,
    const std::vector<int32_t>& iio_event_indices,
    cros::mojom::SensorDevice::GetEventsEnabledCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  event_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&EventsHandler::GetEventsEnabledOnThread,
                                weak_factory_.GetWeakPtr(), client_data,
                                iio_event_indices, std::move(callback)));
}

EventsHandler::EventsHandler(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> event_task_runner,
    libmems::IioDevice* iio_device)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      event_task_runner_(std::move(event_task_runner)),
      iio_device_(iio_device) {}

void EventsHandler::ResetWithReasonOnThread(
    cros::mojom::SensorDeviceDisconnectReason reason, std::string description) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());

  for (ClientData* client : inactive_clients_) {
    if (client->events_observer.is_bound()) {
      client->events_observer.ResetWithReason(static_cast<uint32_t>(reason),
                                              description);
    }
  }
  inactive_clients_.clear();

  for (ClientData* client : active_clients_) {
    if (client->events_observer.is_bound()) {
      client->events_observer.ResetWithReason(static_cast<uint32_t>(reason),
                                              description);
    }
  }
  active_clients_.clear();
}

void EventsHandler::AddClientOnThread(
    ClientData* client_data,
    mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver>
        events_observer) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(client_data->device_data->iio_device, iio_device_);

  if (base::Contains(inactive_clients_, client_data) ||
      base::Contains(active_clients_, client_data)) {
    LOGF(ERROR) << "Failed to AddClient: Already added";
    mojo::Remote<cros::mojom::SensorDeviceEventsObserver>(
        std::move(events_observer))
        ->OnErrorOccurred(cros::mojom::ObserverErrorType::ALREADY_STARTED);
    return;
  }

  DCHECK(!client_data->events_observer.is_bound());
  client_data->events_observer.Bind(std::move(events_observer));
  client_data->events_observer.set_disconnect_handler(
      base::BindOnce(&EventsHandler::OnEventsObserverDisconnect,
                     weak_factory_.GetWeakPtr(), client_data));

  if (client_data->IsEventActive()) {
    AddActiveClientOnThread(client_data);
    return;
  }

  // Adding an inactive client.
  inactive_clients_.emplace(client_data);

  LOGF(ERROR) << "Added an inactive client: No enabled events.";
  client_data->events_observer->OnErrorOccurred(
      cros::mojom::ObserverErrorType::NO_ENABLED_CHANNELS);
}

void EventsHandler::AddActiveClientOnThread(ClientData* client_data) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(client_data->IsEventActive());
  DCHECK(client_data->events_observer.is_bound());
  DCHECK(!base::Contains(inactive_clients_, client_data));
  DCHECK(!base::Contains(active_clients_, client_data));

  active_clients_.emplace(client_data);

  if (!watcher_.get())
    SetEventWatcherOnThread();
}

void EventsHandler::RemoveClientOnThread(ClientData* client_data) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(client_data->device_data->iio_device, iio_device_);

  client_data->events_observer.reset();

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    inactive_clients_.erase(it);
    return;
  }

  if (!base::Contains(active_clients_, client_data)) {
    LOGF(ERROR) << "Failed to RemoveClient: Client not found";
    return;
  }

  RemoveActiveClientOnThread(client_data);
}

void EventsHandler::RemoveActiveClientOnThread(ClientData* client_data) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(base::Contains(active_clients_, client_data));

  active_clients_.erase(client_data);

  if (active_clients_.empty())
    StopEventWatcherOnThread();
}

void EventsHandler::UpdateEventsEnabledOnThread(
    ClientData* client_data,
    const std::vector<int32_t>& iio_event_indices,
    bool en,
    cros::mojom::SensorDevice::SetEventsEnabledCallback callback) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(client_data->device_data->iio_device, iio_device_);

  std::vector<int32_t> failed_indices;

  if (en) {
    for (int32_t event_index : iio_event_indices) {
      auto event = iio_device_->GetEvent(event_index);
      if (!event || !event->IsEnabled()) {
        LOGF(ERROR) << "Failed to enable event with index: " << event_index;
        failed_indices.push_back(event_index);
        continue;
      }

      client_data->enabled_event_indices.emplace(event_index);
    }
  } else {
    for (int32_t event_index : iio_event_indices)
      client_data->enabled_event_indices.erase(event_index);
  }

  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), std::move(failed_indices)));

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    if (client_data->IsEventActive()) {
      // The client is now active.
      inactive_clients_.erase(it);
      AddActiveClientOnThread(client_data);
    }

    return;
  }

  if (!base::Contains(active_clients_, client_data))
    return;

  if (client_data->IsEventActive()) {
    // The client remains active
    return;
  }

  RemoveActiveClientOnThread(client_data);
  inactive_clients_.emplace(client_data);
}

void EventsHandler::GetEventsEnabledOnThread(
    ClientData* client_data,
    const std::vector<int32_t>& iio_event_indices,
    cros::mojom::SensorDevice::GetEventsEnabledCallback callback) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(client_data->device_data->iio_device, iio_device_);

  std::vector<bool> enabled;

  for (int32_t event_index : iio_event_indices) {
    enabled.push_back(
        base::Contains(client_data->enabled_event_indices, event_index));
  }

  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(enabled)));
}

void EventsHandler::OnEventsObserverDisconnect(ClientData* client_data) {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "EventsObserver disconnected. ReceiverId: " << client_data->id;
  RemoveClientOnThread(client_data);
}

void EventsHandler::SetEventWatcherOnThread() {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!watcher_.get());

  auto fd = iio_device_->GetEventFd();
  if (!fd.has_value()) {
    LOGF(ERROR) << "Failed to get fd";
    for (ClientData* client : active_clients_) {
      client->events_observer->OnErrorOccurred(
          cros::mojom::ObserverErrorType::GET_FD_FAILED);
    }

    return;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd.value(),
      base::BindRepeating(&EventsHandler::OnEventAvailableWithoutBlocking,
                          weak_factory_.GetWeakPtr()));
}

void EventsHandler::StopEventWatcherOnThread() {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());

  watcher_.reset();
}

void EventsHandler::OnEventAvailableWithoutBlocking() {
  DCHECK(event_task_runner_->RunsTasksInCurrentSequence());

  auto event = iio_device_->ReadEvent();
  if (!event) {
    for (ClientData* client : active_clients_) {
      client->events_observer->OnErrorOccurred(
          cros::mojom::ObserverErrorType::READ_FAILED);
    }

    return;
  }

  cros::mojom::IioEventPtr iio_event = ExtractIioEvent(event.value());
  base::Optional<int32_t> chn_index;
  for (int32_t i = 0, size = iio_device_->GetAllEvents().size(); i < size;
       ++i) {
    if (iio_device_->GetEvent(i)->MatchMask(event.value().id)) {
      chn_index = i;
      break;
    }
  }
  if (!chn_index.has_value()) {
    LOGF(ERROR) << "No existing events match the mask: " << event.value().id;
    return;
  }

  for (ClientData* client : active_clients_) {
    if (!base::Contains(client->enabled_event_indices, chn_index.value()))
      continue;

    client->events_observer->OnEventUpdated(iio_event.Clone());
  }
}

}  // namespace iioservice
