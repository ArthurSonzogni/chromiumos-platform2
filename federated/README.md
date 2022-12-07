# Chrome OS Federated Computation Service

## Summary

The federated computation service provides a common runtime for federated
analytics (F.A.) and federated learning (F.L.). The service wraps the [federated
computation client] which communicates with the federated computation server,
receives and manages examples from its clients (usually in Chromium) and
schedules the learning/analytics plan. See [go/cros-federated-design] for a
design overview.

## Privacy and Security Review

Each client should have its own privacy & security reviewed launch for usage of
Mojo API to store training data. That's because:

1. Each federated computation method has different security/privacy properties,
   e.g. whether the task has Secure Aggregation enabled.
2. Each type of training data has different privacy considerations when stored
   on the cryptohome, potentially with different TTL requirements.

[federated computation client]: http://go/fcp
[go/cros-federated-design]: http://go/cros-federated-design
