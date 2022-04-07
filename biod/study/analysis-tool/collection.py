#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2022 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import glob
import pathlib
from typing import List, Tuple, Union


class Collection:
    """Interface for the collection directory produced by the fpstudy tool."""

    def __init__(self, collection_dir: Union[pathlib.Path, str]):
        path = pathlib.Path(collection_dir)
        assert path.exists()
        self._collection_dir = path

    def discover_user_groups(self) -> List[Tuple[int, str]]:
        user_groups_set = []
        for path in glob.glob(str(self._collection_dir) + '/*/*'):
            path_split = path.split('/')
            user_id, user_group = path_split[-2], path_split[-1]
            user_groups_set.append((int(user_id), str(user_group)))
        return user_groups_set
