// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;
use std::sync::Arc;

use anyhow::Result;
use serial_test::serial;

use crate::common::{dlc_to_steam_app_id, steam_app_id_to_dlc, SteamAppId};
use crate::dbus_wrapper::{dbus_constants, MockDbusConnectionTrait};
use crate::dlc_queue::new_queue;
use crate::service::periodic_dlc_handler;
use crate::shader_cache_mount::{mount_ops, new_mount_map, VmId};
use crate::test::common::{add_shader_cache_mount, generate_mount_list, mock_gpucache};

fn mock_dbus_conn(
    games_to_install: &[SteamAppId],
    games_to_uninstall: &[SteamAppId],
) -> Arc<MockDbusConnectionTrait> {
    let mut mock_conn = MockDbusConnectionTrait::new();

    let mut dlc_ids_to_install: HashSet<String> = HashSet::new();
    for game_id in games_to_install {
        dlc_ids_to_install.insert(steam_app_id_to_dlc(*game_id));
    }

    let mut dlc_ids_to_uninstall: HashSet<String> = HashSet::new();
    for game_id in games_to_uninstall {
        dlc_ids_to_uninstall.insert(steam_app_id_to_dlc(*game_id));
    }
    mock_conn
        .expect_call_dbus_method()
        .times(games_to_install.len() + games_to_uninstall.len())
        .returning(move |_, _, _, method, (dlc_id,): (String,)| {
            if method == dbus_constants::dlc_service::INSTALL_METHOD {
                assert!(dlc_ids_to_install.remove(&dlc_id));
                return Box::pin(async { Ok(()) });
            }
            if method == dbus_constants::dlc_service::UNINSTALL_METHOD {
                assert!(dlc_ids_to_uninstall.remove(&dlc_id));
                return Box::pin(async { Ok(()) });
            }

            unreachable!();
        });

    Arc::new(mock_conn)
}

#[tokio::test]
async fn dlc_install_one() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[42], &[]);

    dlc_queue.write().await.queue_install(&42);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 1);
    assert!(dlc_queue_read.get_install_queue().contains(&42));
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    periodic_dlc_handler(mount_map, dlc_queue.clone(), dbus_conn).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 1);
    assert!(dlc_queue_read.get_installing_set().contains(&42));

    Ok(())
}

#[tokio::test]
async fn dlc_install_duplicates() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[42], &[]);

    dlc_queue.write().await.queue_install(&42);
    dlc_queue.write().await.queue_install(&42);
    dlc_queue.write().await.queue_install(&42);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 1);
    assert!(dlc_queue_read.get_install_queue().contains(&42));
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    periodic_dlc_handler(mount_map, dlc_queue.clone(), dbus_conn).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 1);
    assert!(dlc_queue_read.get_installing_set().contains(&42));

    Ok(())
}

#[tokio::test]
async fn dlc_install_many_failure_then_success() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let mut dbus_conn = MockDbusConnectionTrait::new();
    dbus_conn.expect_call_dbus_method().times(3).returning(
        move |_, _, _, _, (dlc_id,): (String,)| {
            // Fail every request except id 42
            // TODO(endlesspring): revisit this function, consider putting this
            // into mock_dbus_conn().
            if dlc_to_steam_app_id(&dlc_id).expect("Invalid DLC id") == 42 {
                return Box::pin(async { Ok(()) });
            }
            Box::pin(async { Err(dbus::Error::new_failed("failed")) })
        },
    );

    dlc_queue.write().await.queue_install(&42);
    dlc_queue.write().await.queue_install(&1337);
    dlc_queue.write().await.queue_install(&1234);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 3);
    assert_eq!(dlc_queue_read.get_install_queue().get(0), Some(&1234));
    assert_eq!(dlc_queue_read.get_install_queue().get(1), Some(&1337));
    assert_eq!(dlc_queue_read.get_install_queue().get(2), Some(&42));
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    periodic_dlc_handler(mount_map, dlc_queue.clone(), Arc::new(dbus_conn)).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 1);
    // Only 42 succeeded to install (the first 2 failed)
    assert!(dlc_queue_read.get_installing_set().contains(&42));

    Ok(())
}

#[tokio::test]
async fn dlc_install_one_at_a_time() -> Result<()> {
    // MAX_CONCURRENT_DLC_INSTALLS is set as 1 and won't be changing anytime
    // soon, even if DlcService supports parallel installations to ensure
    // shadercached does not hog up resources.
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[42, 1234, 1337], &[]);

    dlc_queue.write().await.queue_install(&1337);
    dlc_queue.write().await.queue_install(&1234);
    dlc_queue.write().await.queue_install(&42);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 3);
    assert_eq!(dlc_queue_read.get_install_queue().get(0), Some(&42));
    assert_eq!(dlc_queue_read.get_install_queue().get(1), Some(&1234));
    assert_eq!(dlc_queue_read.get_install_queue().get(2), Some(&1337));
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    periodic_dlc_handler(mount_map.clone(), dlc_queue.clone(), dbus_conn.clone()).await;
    // Dlc installation request sent for only 42 since
    // MAX_CONCURRENT_DLC_INSTALLS is 1.
    let mut dlc_queue_write = dlc_queue.write().await;
    assert_eq!(dlc_queue_write.get_install_queue().len(), 2);
    assert_eq!(dlc_queue_write.get_install_queue().get(0), Some(&1234));
    assert_eq!(dlc_queue_write.get_install_queue().get(1), Some(&1337));
    assert_eq!(dlc_queue_write.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_write.get_installing_set().len(), 1);
    assert!(dlc_queue_write.get_installing_set().contains(&42));

    // Clear installing (pretend installation completed, set by
    // handle_dlc_state_changed)
    dlc_queue_write.clear_installing();
    drop(dlc_queue_write);

    periodic_dlc_handler(mount_map.clone(), dlc_queue.clone(), dbus_conn.clone()).await;
    // Dlc installation request sent for only 1234 since
    // MAX_CONCURRENT_DLC_INSTALLS is 1.
    let mut dlc_queue_write = dlc_queue.write().await;
    assert_eq!(dlc_queue_write.get_install_queue().len(), 1);
    assert_eq!(dlc_queue_write.get_install_queue().get(0), Some(&1337));
    assert_eq!(dlc_queue_write.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_write.get_installing_set().len(), 1);
    assert!(dlc_queue_write.get_installing_set().contains(&1234));

    // Clear installing (pretend installation completed, set by
    // handle_dlc_state_changed)
    dlc_queue_write.clear_installing();
    drop(dlc_queue_write);

    periodic_dlc_handler(mount_map.clone(), dlc_queue.clone(), dbus_conn.clone()).await;
    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 1);
    assert!(dlc_queue_read.get_installing_set().contains(&1337));

    Ok(())
}

#[tokio::test]
#[serial(mount_list)]
async fn dlc_uninstall_one_not_mounted() -> Result<()> {
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[], &[42]);

    dlc_queue.write().await.queue_uninstall(&42);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 1);
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(0), Some(&42));
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(1)
        .returning(|| Ok("".to_string()));

    periodic_dlc_handler(mount_map, dlc_queue.clone(), dbus_conn).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);

    Ok(())
}

#[tokio::test]
#[serial(mount_list)]
async fn dlc_uninstall_many_not_mounted() -> Result<()> {
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[], &[42, 1337]);
    let vm_id = VmId {
        vm_owner_id: "owner".to_string(),
        vm_name: "vm".to_string(),
    };

    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    dlc_queue.write().await.queue_uninstall(&42);
    dlc_queue.write().await.queue_uninstall(&1337);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 2);
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(0), Some(&1337));
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(1), Some(&42));
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(2)
        .returning(|| Ok("".to_string()));

    periodic_dlc_handler(mount_map, dlc_queue.clone(), dbus_conn).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);

    Ok(())
}

#[tokio::test]
#[serial(mount_list)]
async fn dlc_uninstall_many_mounted() -> Result<()> {
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[], &[42, 1337, 1234]);
    let vm_id = VmId {
        vm_owner_id: "owner".to_string(),
        vm_name: "vm".to_string(),
    };
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    dlc_queue.write().await.queue_uninstall(&1234);
    dlc_queue.write().await.queue_uninstall(&42);
    dlc_queue.write().await.queue_uninstall(&1337);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 3);
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(0), Some(&1337));
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(1), Some(&42));
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(2), Some(&1234));
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    // Nothing reported as mounted when waiting for unmount
    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(3)
        .returning(|| Ok("".to_string()));
    std::fs::write(
        mock_gpu_cache.path().join("render_server/foz_db_list.txt"),
        "42/foz_cache\n1234/foz_cache\n",
    )?;

    periodic_dlc_handler(mount_map.clone(), dlc_queue.clone(), dbus_conn).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);

    // Queued unmount
    let mount_map_read = mount_map.read().await;
    let shader_cache_mount = mount_map_read.get(&vm_id).unwrap();
    assert_eq!(shader_cache_mount.get_unmount_queue().len(), 2);
    assert!(shader_cache_mount.get_unmount_queue().contains(&42));
    assert!(shader_cache_mount.get_unmount_queue().contains(&1234));

    let foz_db_list_contents =
        std::fs::read_to_string(mock_gpu_cache.path().join("render_server/foz_db_list.txt"))?;
    assert_eq!(foz_db_list_contents.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial(mount_list)]
async fn dlc_uninstall_until_unmount_completed() -> Result<()> {
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[], &[42]);
    let vm_id = VmId {
        vm_owner_id: "owner".to_string(),
        vm_name: "vm".to_string(),
    };
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    dlc_queue.write().await.queue_uninstall(&42);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 1);
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(0), Some(&42));
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    // Report mounted first, then report unmounted
    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    let mount_list = generate_mount_list(&mock_gpu_cache, 42);
    get_mount_list_context
        .expect()
        .times(1)
        .return_once(move || Ok(mount_list));
    get_mount_list_context
        .expect()
        .times(1)
        .return_once(|| Ok("".to_string()));
    std::fs::write(
        mock_gpu_cache.path().join("render_server/foz_db_list.txt"),
        "42/foz_cache\n",
    )?;

    periodic_dlc_handler(mount_map.clone(), dlc_queue.clone(), dbus_conn).await;

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);

    // Queued unmount
    let mount_map_read = mount_map.read().await;
    let shader_cache_mount = mount_map_read.get(&vm_id).unwrap();
    assert_eq!(shader_cache_mount.get_unmount_queue().len(), 1);
    assert!(shader_cache_mount.get_unmount_queue().contains(&42));

    let foz_db_list_contents =
        std::fs::read_to_string(mock_gpu_cache.path().join("render_server/foz_db_list.txt"))?;
    assert_eq!(foz_db_list_contents.len(), 0);

    Ok(())
}

#[tokio::test]
#[serial(mount_list)]
async fn dlc_uninstall_one_mount_queued() -> Result<()> {
    let mock_gpu_cache = mock_gpucache()?;
    let mount_map = new_mount_map();
    let dlc_queue = new_queue();
    let dbus_conn = mock_dbus_conn(&[], &[42, 1337]);
    let vm_id = VmId {
        vm_owner_id: "owner".to_string(),
        vm_name: "vm".to_string(),
    };
    add_shader_cache_mount(&mock_gpu_cache, mount_map.clone(), &vm_id).await?;

    dlc_queue.write().await.queue_uninstall(&42);
    dlc_queue.write().await.queue_uninstall(&1337);

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 2);
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(0), Some(&1337));
    assert_eq!(dlc_queue_read.get_uninstall_queue().get(1), Some(&42));
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);
    drop(dlc_queue_read);

    // Queued mount
    let mut mount_map_mut = mount_map.write().await;
    let shader_cache_mount = mount_map_mut.get_mut(&vm_id).unwrap();
    assert!(shader_cache_mount.enqueue_mount(42));
    assert_eq!(shader_cache_mount.get_mount_queue().len(), 1);
    assert!(shader_cache_mount.get_mount_queue().contains(&42));
    drop(mount_map_mut);

    // Nothing reported as mounted when waiting for unmount
    let get_mount_list_context = mount_ops::helpers::mock_privileged_ops::get_mount_list_context();
    get_mount_list_context
        .expect()
        .times(2)
        .returning(|| Ok("".to_string()));

    periodic_dlc_handler(mount_map.clone(), dlc_queue.clone(), dbus_conn).await;

    // Mount queue is as-is. DlcService will send a signal NOT_INSTALLED
    // after DLC uninstallation and that will trigger mount queue removal along
    // with signal to send mount attempt failed inside handle_dlc_state_changed.
    // This behavior is tested at handle_dlc_state_changed_test.
    let mount_map_read = mount_map.read().await;
    let shader_cache_mount = mount_map_read.get(&vm_id).unwrap();
    assert_eq!(shader_cache_mount.get_mount_queue().len(), 1);
    assert!(shader_cache_mount.get_mount_queue().contains(&42));

    let dlc_queue_read = dlc_queue.read().await;
    assert_eq!(dlc_queue_read.get_uninstall_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_install_queue().len(), 0);
    assert_eq!(dlc_queue_read.get_installing_set().len(), 0);

    Ok(())
}

// TODO(endlesspring): more tests: DLC uninstallation failures at various points
// TODO(endlesspring): probably a new test module - test if delays in DlcService
// DBus calls would cause problems with the periodic handler. We probably need
// to schedule a periodic thread to actually test.
// TODO(endlesspring): test dlc queue with both install and uninstall
// TODO(endlesspring): improve test readability
