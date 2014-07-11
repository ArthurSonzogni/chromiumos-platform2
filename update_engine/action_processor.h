// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_ACTION_PROCESSOR_H_
#define UPDATE_ENGINE_ACTION_PROCESSOR_H_

#include <deque>

#include "base/basictypes.h"

#include "update_engine/error_code.h"

// The structure of these classes (Action, ActionPipe, ActionProcessor, etc.)
// is based on the KSAction* classes from the Google Update Engine code at
// http://code.google.com/p/update-engine/ . The author of this file sends
// a big thanks to that team for their high quality design, implementation,
// and documentation.

// See action.h for an overview of this class and other other Action* classes.

// An ActionProcessor keeps a queue of Actions and processes them in order.

namespace chromeos_update_engine {

class AbstractAction;
class ActionProcessorDelegate;

class ActionProcessor {
 public:
  ActionProcessor();

  virtual ~ActionProcessor();

  // Starts processing the first Action in the queue. If there's a delegate,
  // when all processing is complete, ProcessingDone() will be called on the
  // delegate.
  virtual void StartProcessing();

  // Aborts processing. If an Action is running, it will have
  // TerminateProcessing() called on it. The Action that was running
  // will be lost and must be re-enqueued if this Processor is to use it.
  void StopProcessing();

  // Returns true iff an Action is currently processing.
  bool IsRunning() const { return NULL != current_action_; }

  // Adds another Action to the end of the queue.
  virtual void EnqueueAction(AbstractAction* action);

  // Sets/gets the current delegate. Set to NULL to remove a delegate.
  ActionProcessorDelegate* delegate() const { return delegate_; }
  void set_delegate(ActionProcessorDelegate *delegate) {
    delegate_ = delegate;
  }

  // Returns a pointer to the current Action that's processing.
  AbstractAction* current_action() const {
    return current_action_;
  }

  // Called by an action to notify processor that it's done. Caller passes self.
  void ActionComplete(AbstractAction* actionptr, ErrorCode code);

 private:
  // Actions that have not yet begun processing, in the order in which
  // they'll be processed.
  std::deque<AbstractAction*> actions_;

  // A pointer to the currrently processing Action, if any.
  AbstractAction* current_action_;

  // A pointer to the delegate, or NULL if none.
  ActionProcessorDelegate *delegate_;
  DISALLOW_COPY_AND_ASSIGN(ActionProcessor);
};

// A delegate object can be used to be notified of events that happen
// in an ActionProcessor. An instance of this class can be passed to an
// ActionProcessor to register itself.
class ActionProcessorDelegate {
 public:
  // Called when all processing in an ActionProcessor has completed. A pointer
  // to the ActionProcessor is passed. |code| is set to the exit code of the
  // last completed action.
  virtual void ProcessingDone(const ActionProcessor* processor,
                              ErrorCode code) {}

  // Called when processing has stopped. Does not mean that all Actions have
  // completed. If/when all Actions complete, ProcessingDone() will be called.
  virtual void ProcessingStopped(const ActionProcessor* processor) {}

  // Called whenever an action has finished processing, either successfully
  // or otherwise.
  virtual void ActionCompleted(ActionProcessor* processor,
                               AbstractAction* action,
                               ErrorCode code) {}
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_ACTION_PROCESSOR_H_
