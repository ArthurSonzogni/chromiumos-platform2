# Heartd (Health Ensure and Accident Resolve Treatment)

`heartd` is a service layer watchdog. It provides interface for other apps or
services to send heartbeat to us. When there is missing heartbeat, it takes
corresponding actions to restore the device.

We expect that clients only need to rely on `heartd`. For system-wise problem
(e.g. kernel hang, etc), it's resolved by others. (e.g. CPU lock up detector,
daisydog, etc)

[TOC]

## How to start the service

This service is not running by default. It's a D-Bus service in current
implementation. To start up, you have to send the signal to D-Bus and then it
starts running.

The following is an example to show how to run it in command line.

```
(DUT) $ dbus-send --system --type=signal --dest=org.chromium.Heartd
        /org/chromium/Heartd org.chromium.Heartd
```

## How to register the service

Using the service is simple.

However, to add a new service name (in step 4), we need to add it into the mojo
interface and this needs a security review. So please reach out to the OWNER as
early as possible.

1. Add dependency.
    - For clients in ash Chrome: Add the following into your dependency.
        - `//chromeos/ash/services/heartd/public/mojom`
        - `//chromeos/ash/components/mojo_service_manager`
    - For clients in platform: Add the following dependency in your ebuild.
        - `chromeos-base/heartd:=`
        - `chromeos-base/mojo_service_manager:=`

2. Create an object whose life cycle is equal to or larger than your service or
   app. Add mojo remote as the object members. For example:
    - `mojo::Remote<ash::heartd::mojom::HeartbeatService> hb_remote_;`
    - `mojo::Remote<ash::heartd::mojom::HeartdControl> heartd_control_remote_;`
    - `mojo::Remote<ash::heartd::mojom::Pacemaker> pacemaker_;`

3. Request the remote through mojo service manager.
    ```
    #include "chromeos/ash/components/mojo_service_manager/connection.h"

    mojo_service_manager::GetServiceManagerProxy()->Request(
        chromeos::mojo_services::kHeartdHeartbeatService, std::nullopt,
        hb_remote_.BindNewPipeAndPassReceiver().PassPipe());
    mojo_service_manager::GetServiceManagerProxy()->Request(
        chromeos::mojo_services::kHeartdControl, std::nullopt,
        heartd_control_remote_.BindNewPipeAndPassReceiver().PassPipe());
    ```

4. Register the heartbeat service.
    ```
    ash::heartd::mojom::HeartbeatServiceArgument argument;
    // Set up argument.
    // ...

    hb_remote_->Register(ash::heartd::mojom::ServiceName::kServiceName,
                         std::move(argument),
                         pacemaker_.BindNewPipeAndPassReceiver(),
                         callback);
    ```

5. Start sending heartbeat.
    ```
    pacemaker_->SendHeartbeat(callback);
    ```

## Links

1. [Design Doc](http://go/cros-heartd)
