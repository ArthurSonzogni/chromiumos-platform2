# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""House the classes for working with fpstudy collection directories."""

from __future__ import annotations

import glob
import pathlib
from typing import Union


class Collection:
    """Interface for the collection directory produced by the fpstudy tool."""

    def __init__(self, collection_dir: Union[pathlib.Path, str]):
        path = pathlib.Path(collection_dir)
        assert path.exists()
        self._collection_dir = path

    def discover_user_groups(self) -> list[tuple[int, str]]:
        user_groups_set: list[tuple[int, str]] = []
        for path in glob.glob(str(self._collection_dir) + "/*/*"):
            path_split = path.split("/")
            user_id, user_group = path_split[-2], path_split[-1]
            user_groups_set.append((int(user_id), str(user_group)))
        return user_groups_set
