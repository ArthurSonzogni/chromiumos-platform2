// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod chromeos;

use std::error::Error;
use std::fmt;

pub use self::chromeos::ChromeOS;

struct UnimplementedError {
    name: &'static str,
    function: &'static str,
}

impl fmt::Display for UnimplementedError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "backend `{}` does not implement `{}`",
            self.name, self.function
        )
    }
}

impl fmt::Debug for UnimplementedError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        <Self as fmt::Display>::fmt(self, f)
    }
}

impl Error for UnimplementedError {}

#[derive(Default)]
pub struct VmFeatures {
    pub gpu: bool,
    pub software_tpm: bool,
}

// The input for this macro is an ordinary trait declaration, with some restrictions. Each method
// must take `&mut self` and return a `Result` where the `Ok` variant implements `Default` and the
// `Err` variant is `Box<Error>`. All other arguments types must implement `Default` and no provided
// implementations for the methods are allowed.
//
// The output of this macro is the input trait with provided methods that return
// `UnimplementedError`. The macro will also produce an implementation of the trait that uses only
// provided methods (i.e. returns an error) and an implementation that only returns
// `Ok(Default::default())`. Each dummy implementation also gets a suite of tests.
macro_rules! impl_backend {
    ($(#[$trait_attr:meta])* pub trait $trait_nm:ident {
        $( $(#[$fn_attr:meta])* fn $fn_nm:ident (&mut self $(, $arg_nm:ident: $arg_ty:ty)* $(,)*) -> Result<$fn_ret:ty, Box<Error>>;)+
    }) => {

        $(#[$trait_attr])*
        pub trait $trait_nm {
            /// Returns the name of this implementation.
            fn name(&self) -> &'static str;

            $(
                $(#[$fn_attr])*
                fn $fn_nm(&mut self $(, $arg_nm: $arg_ty)*) -> Result<$fn_ret, Box<Error>> {
                    $(let _ = $arg_nm;)*
                    Err(UnimplementedError {
                        name: self.name(),
                        function: stringify!($fn_nm)
                    }.into())
                }
            )+
        }

        /// A backend that has no side effects and returns an error for every method call.
        #[derive(Default)]
        pub struct DummyUnimplementedBackend;
        impl DummyUnimplementedBackend {
            /// Creates a dummy backend with no implementations.
            pub fn new() -> DummyUnimplementedBackend {
                DummyUnimplementedBackend
            }
        }

        impl $trait_nm for DummyUnimplementedBackend {
            fn name(&self) -> &'static str {
                "Dummy Unimplemented"
            }
        }


        #[cfg(test)]
        mod dummy_unimplemented_backend_tests {
            use super::*;

            $(
                #[test]
                fn $fn_nm() {
                    let mut backend = DummyUnimplementedBackend::new();
                    backend.$fn_nm( $( <$arg_ty as Default>::default(), )* ).expect_err("dummy method succeeded");
                }
            )+
        }

        /// A backend that always returns an `Ok` result with a default value and has no side
        /// effects.
        #[derive(Default)]
        pub struct DummyDefaultBackend;
        impl DummyDefaultBackend {
            pub fn new() -> DummyDefaultBackend {
                DummyDefaultBackend
            }
        }

        impl $trait_nm for DummyDefaultBackend {
            fn name(&self) -> &'static str {
                "Dummy Default"
            }

            $(
                fn $fn_nm(&mut self $(, $arg_nm: $arg_ty)*) -> Result<$fn_ret, Box<Error>> {
                    $(let _ = $arg_nm;)*
                    Ok(Default::default())
                }
            )+
        }

        #[cfg(test)]
        mod dummy_default_backend_tests {
            use super::*;

            $(
                #[test]
                fn $fn_nm() {
                    let mut backend = DummyDefaultBackend::new();
                    backend.$fn_nm( $( <$arg_ty as Default>::default(), )* ).unwrap();
                }
            )+
        }

    }
}

impl_backend! {
    /// Defines the method and provides a default implementation that returns an
    /// `UnimplementedError`.
    pub trait Backend {
        // Metrics

        /// Sends a `Platform.CrOSEvent` enum histogram sample.
        fn metrics_send_sample(&mut self, name: &str) -> Result<(), Box<Error>>;

        // Sessions
        /// Gets a list of active sessions as a list of tuples: `(email, user_id_hash)`.
        fn sessions_list(&mut self) -> Result<Vec<(String, String)>, Box<Error>>;

        // Vm

        /// Starts a VM called `name` with the given `user_id_hash`.
        fn vm_start(
            &mut self,
            name: &str,
            user_id_hash: &str,
            features: VmFeatures,
        ) -> Result<(), Box<Error>>;
        /// Stop `name` VM with given `user_id_hash`.
        fn vm_stop(&mut self, name: &str, user_id_hash: &str) -> Result<(), Box<Error>>;
        /// Exports the stateful disk image of `name` VM owned by `user_id_hash` to `file_name`,
        /// optionally to external drive `removable_media`.
        fn vm_export(
            &mut self,
            name: &str,
            user_id_hash: &str,
            file_name: &str,
            removable_media: Option<&str>,
        ) -> Result<Option<String>, Box<Error>>;
        /// Imports a disk image `file_name` as a VM `name` owned by `user_id_hash`. The file may
        /// optionally reside on external drive `removable_media`.
        fn vm_import(
            &mut self,
            name: &str,
            user_id_hash: &str,
            plugin_vm: bool,
            file_name: &str,
            removable_media: Option<&str>,
        ) -> Result<Option<String>, Box<Error>>;
        /// Share a `path` with VM `name` owned by `user_id_hash` and return the path inside the VM
        /// that it was mounted.
        fn vm_share_path(
            &mut self,
            name: &str,
            user_id_hash: &str,
            path: &str,
        ) -> Result<String, Box<Error>>;

        // Vsh

        /// Starts `vsh` to open a shell into `vm_name` owned by `user_id_hash`.
        fn vsh_exec(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>>;
        /// Opens virtual shell in `container_name`, inside the `vm_name`, owned by `user_id_hash`.
        fn vsh_exec_container(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
            container_name: &str,
        ) -> Result<(), Box<Error>>;

        // Disk

        /// Destroys the disk of `vm_name` for the given `user_id_hash`.
        fn disk_destroy(&mut self, vm_name: &str, user_id_hash: &str) -> Result<(), Box<Error>>;
        /// Gets a list of all VM disks for `user_id_hash` and their total size.
        fn disk_list(
            &mut self,
            user_id_hash: &str,
        ) -> Result<(Vec<(String, u64)>, u64), Box<Error>>;
        /// Checks status of executing disk operation (import, export).
        fn disk_op_status(
            &mut self,
            uuid: &str,
            user_id_hash: &str,
        ) -> Result<(bool, u32), Box<Error>>;
        /// Waits for the status of executing disk operation (import, export) to update.
        fn wait_disk_op(
            &mut self,
            uuid: &str,
            user_id_hash: &str,
        ) -> Result<(bool, u32), Box<Error>>;

        // Container

        /// Creates `container_name` ,inside the `vm_name`, owned by `user_id_hash`, derived from
        /// `image_alias` from the `image_server`.
        fn container_create(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
            container_name: &str,
            image_server: &str,
            image_alias: &str,
        ) -> Result<(), Box<Error>>;

        /// Starts `container_name`, inside the `vm_name`, owned by `user_id_hash`.
        fn container_start(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
            container_name: &str,
        ) -> Result<(), Box<Error>>;

        /// Sets up the `username` in `container_name`, inside the `vm_name`, owned by
        /// `user_id_hash`.
        fn container_setup_user(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
            container_name: &str,
            username: &str,
        ) -> Result<(), Box<Error>>;

        // USB
        fn usb_attach(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
            bus: u8,
            device: u8,
        ) -> Result<u8, Box<Error>>;
        fn usb_detach(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
            port: u8,
        ) -> Result<(), Box<Error>>;
        fn usb_list(
            &mut self,
            vm_name: &str,
            user_id_hash: &str,
        ) -> Result<Vec<(u8, u16, u16, String)>, Box<Error>>;
    }
}
