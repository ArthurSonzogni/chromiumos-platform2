extern crate anyhow;
extern crate dbus as dbus_crate;
extern crate lazy_static;

mod common;
mod dbus;
mod memory;

#[cfg(test)]
mod test;

use anyhow::Result;

fn main() -> Result<()> {
    dbus::start_service()
}
