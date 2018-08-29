# CrosML: Chrome OS Machine Learning Service

## Summary

The Machine Learning (ML) Service provides a common runtime for evaluating
machine learning models on device. The service wraps the TensorFlow Lite runtime
and provides infrastructure for deployment of trained models. Chromium
communicates with ML Service via a Mojo Interface.

## How to use ML Service

You need to provide your trained models to ML Service by following [these
instructions](docs/publish_model.md).
You can then load and use your model from Chromium using the client library
provided at [//chromeos/services/machine_learning/public/cpp/].

## Design docs

* Overall design: [go/chromeos-ml-service]
* Mojo interface: [go/chromeos-ml-service-mojo]
* Deamon\<-\>Chromium IPC implementation: [go/chromeos-ml-service-impl]
* Model publishing: [go/cros-ml-service-models]


[go/chromeos-ml-service]: http://go/chromeos-ml-service
[go/chromeos-ml-service-mojo]: http://go/chromeos-ml-service-mojo
[go/chromeos-ml-service-impl]: http://go/chromeos-ml-service-impl
[go/cros-ml-service-models]: http://go/cros-ml-service-models
[//chromeos/services/machine_learning/public/cpp/]: https://cs.chromium.org/chromium/src/chromeos/services/machine_learning/public/cpp/service_connection.h
