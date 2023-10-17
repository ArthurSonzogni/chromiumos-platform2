# ChromeOS Federated Computation Service

The federated computation service provides a common runtime for federated
analytics (F.A.) and federated learning (F.L.). The service wraps the [federated
computation client] which communicates with the federated computation server,
receives and manages examples from its clients (usually in Chromium) and
schedules the learning/analytics plan.

For more information, see [detailed docs].

[federated computation client]: https://github.com/google/federated-compute/tree/main/fcp
[detailed docs]: http://go/cros-federated-docs-details
