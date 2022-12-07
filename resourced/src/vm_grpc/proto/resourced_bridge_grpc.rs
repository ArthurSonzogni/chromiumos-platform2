// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is generated. Do not edit
// @generated

// https://github.com/Manishearth/rust-clippy/issues/702
#![allow(unknown_lints)]
#![allow(clippy::all)]

#![cfg_attr(rustfmt, rustfmt_skip)]

#![allow(box_pointers)]
#![allow(dead_code)]
#![allow(missing_docs)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]
#![allow(trivial_casts)]
#![allow(unsafe_code)]
#![allow(unused_imports)]
#![allow(unused_results)]

const METHOD_RESOURCED_COMM_LISTENER_START_CPU_UPDATES: ::grpcio::Method<super::resourced_bridge::RequestedInterval, super::resourced_bridge::ReturnCode> = ::grpcio::Method {
    ty: ::grpcio::MethodType::Unary,
    name: "/resourced_bridge.ResourcedCommListener/StartCpuUpdates",
    req_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
    resp_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
};

const METHOD_RESOURCED_COMM_LISTENER_STOP_CPU_UPDATES: ::grpcio::Method<super::resourced_bridge::EmptyMessage, super::resourced_bridge::ReturnCode> = ::grpcio::Method {
    ty: ::grpcio::MethodType::Unary,
    name: "/resourced_bridge.ResourcedCommListener/StopCpuUpdates",
    req_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
    resp_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
};

const METHOD_RESOURCED_COMM_LISTENER_SET_CPU_FREQUENCY: ::grpcio::Method<super::resourced_bridge::RequestedCpuFrequency, super::resourced_bridge::ReturnCode> = ::grpcio::Method {
    ty: ::grpcio::MethodType::Unary,
    name: "/resourced_bridge.ResourcedCommListener/SetCpuFrequency",
    req_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
    resp_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
};

#[derive(Clone)]
pub struct ResourcedCommListenerClient {
    client: ::grpcio::Client,
}

impl ResourcedCommListenerClient {
    pub fn new(channel: ::grpcio::Channel) -> Self {
        ResourcedCommListenerClient {
            client: ::grpcio::Client::new(channel),
        }
    }

    pub fn start_cpu_updates_opt(&self, req: &super::resourced_bridge::RequestedInterval, opt: ::grpcio::CallOption) -> ::grpcio::Result<super::resourced_bridge::ReturnCode> {
        self.client.unary_call(&METHOD_RESOURCED_COMM_LISTENER_START_CPU_UPDATES, req, opt)
    }

    pub fn start_cpu_updates(&self, req: &super::resourced_bridge::RequestedInterval) -> ::grpcio::Result<super::resourced_bridge::ReturnCode> {
        self.start_cpu_updates_opt(req, ::grpcio::CallOption::default())
    }

    pub fn start_cpu_updates_async_opt(&self, req: &super::resourced_bridge::RequestedInterval, opt: ::grpcio::CallOption) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::ReturnCode>> {
        self.client.unary_call_async(&METHOD_RESOURCED_COMM_LISTENER_START_CPU_UPDATES, req, opt)
    }

    pub fn start_cpu_updates_async(&self, req: &super::resourced_bridge::RequestedInterval) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::ReturnCode>> {
        self.start_cpu_updates_async_opt(req, ::grpcio::CallOption::default())
    }

    pub fn stop_cpu_updates_opt(&self, req: &super::resourced_bridge::EmptyMessage, opt: ::grpcio::CallOption) -> ::grpcio::Result<super::resourced_bridge::ReturnCode> {
        self.client.unary_call(&METHOD_RESOURCED_COMM_LISTENER_STOP_CPU_UPDATES, req, opt)
    }

    pub fn stop_cpu_updates(&self, req: &super::resourced_bridge::EmptyMessage) -> ::grpcio::Result<super::resourced_bridge::ReturnCode> {
        self.stop_cpu_updates_opt(req, ::grpcio::CallOption::default())
    }

    pub fn stop_cpu_updates_async_opt(&self, req: &super::resourced_bridge::EmptyMessage, opt: ::grpcio::CallOption) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::ReturnCode>> {
        self.client.unary_call_async(&METHOD_RESOURCED_COMM_LISTENER_STOP_CPU_UPDATES, req, opt)
    }

    pub fn stop_cpu_updates_async(&self, req: &super::resourced_bridge::EmptyMessage) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::ReturnCode>> {
        self.stop_cpu_updates_async_opt(req, ::grpcio::CallOption::default())
    }

    pub fn set_cpu_frequency_opt(&self, req: &super::resourced_bridge::RequestedCpuFrequency, opt: ::grpcio::CallOption) -> ::grpcio::Result<super::resourced_bridge::ReturnCode> {
        self.client.unary_call(&METHOD_RESOURCED_COMM_LISTENER_SET_CPU_FREQUENCY, req, opt)
    }

    pub fn set_cpu_frequency(&self, req: &super::resourced_bridge::RequestedCpuFrequency) -> ::grpcio::Result<super::resourced_bridge::ReturnCode> {
        self.set_cpu_frequency_opt(req, ::grpcio::CallOption::default())
    }

    pub fn set_cpu_frequency_async_opt(&self, req: &super::resourced_bridge::RequestedCpuFrequency, opt: ::grpcio::CallOption) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::ReturnCode>> {
        self.client.unary_call_async(&METHOD_RESOURCED_COMM_LISTENER_SET_CPU_FREQUENCY, req, opt)
    }

    pub fn set_cpu_frequency_async(&self, req: &super::resourced_bridge::RequestedCpuFrequency) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::ReturnCode>> {
        self.set_cpu_frequency_async_opt(req, ::grpcio::CallOption::default())
    }
    pub fn spawn<F>(&self, f: F) where F: ::futures::Future<Output = ()> + Send + 'static {
        self.client.spawn(f)
    }
}

pub trait ResourcedCommListener {
    fn start_cpu_updates(&mut self, ctx: ::grpcio::RpcContext, req: super::resourced_bridge::RequestedInterval, sink: ::grpcio::UnarySink<super::resourced_bridge::ReturnCode>);
    fn stop_cpu_updates(&mut self, ctx: ::grpcio::RpcContext, req: super::resourced_bridge::EmptyMessage, sink: ::grpcio::UnarySink<super::resourced_bridge::ReturnCode>);
    fn set_cpu_frequency(&mut self, ctx: ::grpcio::RpcContext, req: super::resourced_bridge::RequestedCpuFrequency, sink: ::grpcio::UnarySink<super::resourced_bridge::ReturnCode>);
}

pub fn create_resourced_comm_listener<S: ResourcedCommListener + Send + Clone + 'static>(s: S) -> ::grpcio::Service {
    let mut builder = ::grpcio::ServiceBuilder::new();
    let mut instance = s.clone();
    builder = builder.add_unary_handler(&METHOD_RESOURCED_COMM_LISTENER_START_CPU_UPDATES, move |ctx, req, resp| {
        instance.start_cpu_updates(ctx, req, resp)
    });
    let mut instance = s.clone();
    builder = builder.add_unary_handler(&METHOD_RESOURCED_COMM_LISTENER_STOP_CPU_UPDATES, move |ctx, req, resp| {
        instance.stop_cpu_updates(ctx, req, resp)
    });
    let mut instance = s;
    builder = builder.add_unary_handler(&METHOD_RESOURCED_COMM_LISTENER_SET_CPU_FREQUENCY, move |ctx, req, resp| {
        instance.set_cpu_frequency(ctx, req, resp)
    });
    builder.build()
}

const METHOD_RESOURCED_COMM_VM_INIT_DATA: ::grpcio::Method<super::resourced_bridge::InitData, super::resourced_bridge::EmptyMessage> = ::grpcio::Method {
    ty: ::grpcio::MethodType::Unary,
    name: "/resourced_bridge.ResourcedComm/VmInitData",
    req_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
    resp_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
};

const METHOD_RESOURCED_COMM_CPU_POWER_UPDATE: ::grpcio::Method<super::resourced_bridge::CpuRaplPowerData, super::resourced_bridge::EmptyMessage> = ::grpcio::Method {
    ty: ::grpcio::MethodType::Unary,
    name: "/resourced_bridge.ResourcedComm/CpuPowerUpdate",
    req_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
    resp_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
};

const METHOD_RESOURCED_COMM_BATTERY_UPDATE: ::grpcio::Method<super::resourced_bridge::BatteryData, super::resourced_bridge::EmptyMessage> = ::grpcio::Method {
    ty: ::grpcio::MethodType::Unary,
    name: "/resourced_bridge.ResourcedComm/BatteryUpdate",
    req_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
    resp_mar: ::grpcio::Marshaller { ser: ::grpcio::pb_ser, de: ::grpcio::pb_de },
};

#[derive(Clone)]
pub struct ResourcedCommClient {
    client: ::grpcio::Client,
}

impl ResourcedCommClient {
    pub fn new(channel: ::grpcio::Channel) -> Self {
        ResourcedCommClient {
            client: ::grpcio::Client::new(channel),
        }
    }

    pub fn vm_init_data_opt(&self, req: &super::resourced_bridge::InitData, opt: ::grpcio::CallOption) -> ::grpcio::Result<super::resourced_bridge::EmptyMessage> {
        self.client.unary_call(&METHOD_RESOURCED_COMM_VM_INIT_DATA, req, opt)
    }

    pub fn vm_init_data(&self, req: &super::resourced_bridge::InitData) -> ::grpcio::Result<super::resourced_bridge::EmptyMessage> {
        self.vm_init_data_opt(req, ::grpcio::CallOption::default())
    }

    pub fn vm_init_data_async_opt(&self, req: &super::resourced_bridge::InitData, opt: ::grpcio::CallOption) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::EmptyMessage>> {
        self.client.unary_call_async(&METHOD_RESOURCED_COMM_VM_INIT_DATA, req, opt)
    }

    pub fn vm_init_data_async(&self, req: &super::resourced_bridge::InitData) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::EmptyMessage>> {
        self.vm_init_data_async_opt(req, ::grpcio::CallOption::default())
    }

    pub fn cpu_power_update_opt(&self, req: &super::resourced_bridge::CpuRaplPowerData, opt: ::grpcio::CallOption) -> ::grpcio::Result<super::resourced_bridge::EmptyMessage> {
        self.client.unary_call(&METHOD_RESOURCED_COMM_CPU_POWER_UPDATE, req, opt)
    }

    pub fn cpu_power_update(&self, req: &super::resourced_bridge::CpuRaplPowerData) -> ::grpcio::Result<super::resourced_bridge::EmptyMessage> {
        self.cpu_power_update_opt(req, ::grpcio::CallOption::default())
    }

    pub fn cpu_power_update_async_opt(&self, req: &super::resourced_bridge::CpuRaplPowerData, opt: ::grpcio::CallOption) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::EmptyMessage>> {
        self.client.unary_call_async(&METHOD_RESOURCED_COMM_CPU_POWER_UPDATE, req, opt)
    }

    pub fn cpu_power_update_async(&self, req: &super::resourced_bridge::CpuRaplPowerData) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::EmptyMessage>> {
        self.cpu_power_update_async_opt(req, ::grpcio::CallOption::default())
    }

    pub fn battery_update_opt(&self, req: &super::resourced_bridge::BatteryData, opt: ::grpcio::CallOption) -> ::grpcio::Result<super::resourced_bridge::EmptyMessage> {
        self.client.unary_call(&METHOD_RESOURCED_COMM_BATTERY_UPDATE, req, opt)
    }

    pub fn battery_update(&self, req: &super::resourced_bridge::BatteryData) -> ::grpcio::Result<super::resourced_bridge::EmptyMessage> {
        self.battery_update_opt(req, ::grpcio::CallOption::default())
    }

    pub fn battery_update_async_opt(&self, req: &super::resourced_bridge::BatteryData, opt: ::grpcio::CallOption) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::EmptyMessage>> {
        self.client.unary_call_async(&METHOD_RESOURCED_COMM_BATTERY_UPDATE, req, opt)
    }

    pub fn battery_update_async(&self, req: &super::resourced_bridge::BatteryData) -> ::grpcio::Result<::grpcio::ClientUnaryReceiver<super::resourced_bridge::EmptyMessage>> {
        self.battery_update_async_opt(req, ::grpcio::CallOption::default())
    }
    pub fn spawn<F>(&self, f: F) where F: ::futures::Future<Output = ()> + Send + 'static {
        self.client.spawn(f)
    }
}

pub trait ResourcedComm {
    fn vm_init_data(&mut self, ctx: ::grpcio::RpcContext, req: super::resourced_bridge::InitData, sink: ::grpcio::UnarySink<super::resourced_bridge::EmptyMessage>);
    fn cpu_power_update(&mut self, ctx: ::grpcio::RpcContext, req: super::resourced_bridge::CpuRaplPowerData, sink: ::grpcio::UnarySink<super::resourced_bridge::EmptyMessage>);
    fn battery_update(&mut self, ctx: ::grpcio::RpcContext, req: super::resourced_bridge::BatteryData, sink: ::grpcio::UnarySink<super::resourced_bridge::EmptyMessage>);
}

pub fn create_resourced_comm<S: ResourcedComm + Send + Clone + 'static>(s: S) -> ::grpcio::Service {
    let mut builder = ::grpcio::ServiceBuilder::new();
    let mut instance = s.clone();
    builder = builder.add_unary_handler(&METHOD_RESOURCED_COMM_VM_INIT_DATA, move |ctx, req, resp| {
        instance.vm_init_data(ctx, req, resp)
    });
    let mut instance = s.clone();
    builder = builder.add_unary_handler(&METHOD_RESOURCED_COMM_CPU_POWER_UPDATE, move |ctx, req, resp| {
        instance.cpu_power_update(ctx, req, resp)
    });
    let mut instance = s;
    builder = builder.add_unary_handler(&METHOD_RESOURCED_COMM_BATTERY_UPDATE, move |ctx, req, resp| {
        instance.battery_update(ctx, req, resp)
    });
    builder.build()
}
