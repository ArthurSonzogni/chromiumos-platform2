# ML Service seccomp configs

ML Service has individual sandbox configs for each ML model and so the seccomp
allowlist setup is a bit more complex.

The `.policy` files in this directory fall into three categories as follows:

* `ml_service-seccomp-$ARCH.policy`: Overall syscall allowlist for the normal
  Mojo ML Service at the time the service is launched. Used by
  `../init/ml-service.conf`. By necessity, this includes all syscalls used by
  any of the narrowed allowlists mentioned below.
* `ml_service-$SPECIFIC_MODEL-seccomp-$ARCH.policy`: Narrowed syscall allowlist
  for ML Service subprocess sandboxes for running a specific model. ML Service
  enters this seccomp policy after it spawns a subprocess. See
  `SetSeccompPolicyPath` in `../process.cc`.
* `ml_service-AdaptiveChargingModel-seccomp-$ARCH.policy`: Special case
  top-level allowlist used by `../init/ml-service.conf` when ML Service is
  launched in its non-Mojo service mode for use outside Chrome.
