#!/usr/bin/env python3
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Ensure source files are not missed and included in a gn file.

Occasionally, a source file, especially a unit test file, is added to the repo
but is not included in the build. This kind of mistake can often evade code
review, or is discovered only later in the review stage and wasted time for
early discovery of errors. This script examines each source file and checks
whether they are present in a *.gn file in their project directory.

Additionally, when a build file is changed, occasionally some source files
(especially test files) are left unbuilt. This script also checks for source
files that are not included anywhere when a build file is modified.
"""

import argparse
import os
from pathlib import Path
from pathlib import PurePath
import sys
from typing import FrozenSet, Iterable, List, Optional, Tuple, Union


_HACK_VAR_TO_DISABLE_ISORT = "hack"
# pylint: disable=wrong-import-position
import chromite_init  # pylint: disable=unused-import

from chromite.lib import cros_build_lib
from chromite.lib import git
from chromite.lint.linters import gnlint


TOP_DIR = Path(__file__).resolve().parent.parent
SOURCE_FILE_SUFFICES = (".c", ".cc", ".cpp", ".cxx")

# common-mk example files that are not intended to be part of GN build.
EXCLUDED_FILES = frozenset(
    (
        "common-mk/example/component/component.cc",
        "common-mk/example/component/subcomponent/subcomponent.c",
        "common-mk/example/project_main.cc",
    )
)


class ProjectLiterals:
    """Manages project literals."""

    def __init__(self, commit: str):
        # A map that saves project literals.
        self._literals: map = {}
        self._commit: str = commit

    def GetLiterals(self, project: str) -> FrozenSet[PurePath]:
        """Get the literal source files from a project.

        If the literals have not gathered yet, gather them.

        Args:
            project: The project to get literals from.

        Returns:
            Literals from the specified project.
        """
        if project not in self._literals:
            self._literals[project] = self._GatherLiteralsFromProject(project)

        return self._literals[project]

    def _GatherLiteralsFromProject(self, project: str) -> FrozenSet[PurePath]:
        """Gather the literal source files from a project.

        Args:
            project: The project to gather source file literals from.

        Returns:
            A set of all source file literals from a project.
        """

        def _gather() -> Iterable[Tuple[PurePath, PurePath]]:
            """Generate the gn file path and its source file literals."""
            for gn_file in self._FindGnFiles(project):
                yield from (
                    (gn_file, literal)
                    for literal in ProjectLiterals._GatherLiteralsFromGn(
                        self._ReadFileAtCommit(project, gn_file)
                    )
                )

        def _resolve(gn_file: PurePath, literal: PurePath) -> PurePath:
            """Resolve a source file literal relative to the top dir."""
            return (
                (
                    TOP_DIR
                    / project
                    / PurePath(gn_file).parent
                    / gnlint.GetNodeValue(literal)
                )
                .resolve()
                .relative_to(TOP_DIR)
            )

        return frozenset(
            _resolve(gn_file, literal) for gn_file, literal in _gather()
        )

    def _ReadFileAtCommit(self, project: str, file_path: os.PathLike) -> str:
        """Read a file at a commit.

        Args:
            project: The project to read the file from.
            file_path: The path to the file to read. Path is relative to the
            project root directory.

        Returns:
            The content of the file.
        """
        return git.GetObjectAtRev(
            TOP_DIR / project, project / file_path, self._commit
        )

    def _FindFilesEndingWith(
        self, project: str, suffix: Union[str, Tuple[str]]
    ) -> Iterable[PurePath]:
        """Find all gn files in a project.

        Args:
            project: The project to find files in.
            suffix: The suffix that files end with. Can also be a tuple of
                    suffices.

        Returns:
            An iterable of all files ending with the given suffix.
        """
        yield from (
            f.name
            for f in git.LsTree(TOP_DIR / project, self._commit)
            if f.is_file and f.name.name.endswith(suffix)
        )

    def _FindGnFiles(self, project: str) -> Iterable[PurePath]:
        """Find all gn files in a project.

        Args:
            project: The project to find gn files in.

        Returns:
            An iterable of all gn files.
        """
        yield from self._FindFilesEndingWith(project, ".gn")

    def FindSourceFiles(self, project: str) -> Iterable[PurePath]:
        """Find all source files in a project.

        Args:
            project: The project to find source files in.

        Returns:
            An iterable of all source files.
        """
        yield from self._FindFilesEndingWith(project, SOURCE_FILE_SUFFICES)

    @staticmethod
    def _GatherLiteralsFromGn(gn_data: str) -> List[dict]:
        """Gather all source file literals from a gn file.

        Args:
            gn_data: The content of a gn file to gather literals from.

        Returns:
            A list of literal assignments.
        """
        try:
            ast = gnlint.ParseAst(gn_data)
        except cros_build_lib.RunCommandError as e:
            cros_build_lib.Die("Failed to run gn format: %s", e)
        except Exception as e:
            cros_build_lib.Die("Invalid format: %s", e)

        return gnlint.FindAllLiteralAssignments(
            ast, ["sources"], operators=["=", "+="]
        )


def CheckSourceFileIncludedInBuild(
    commit: str, file_paths: Iterable[Union[str, os.PathLike]]
) -> bool:
    """Check that source files are included in builds.

    Args:
        commit: The commit to check in.
        file_paths: Files modified in this commit. Non-source files in this list
                    would be ignored.

    Returns:
        True if source files are included in a *.gn file in the project
        directory. False otherwise.
    """

    ret = True
    project_literals = ProjectLiterals(commit)

    for path in file_paths:
        path = PurePath(path)
        if not path.name.endswith(SOURCE_FILE_SUFFICES):
            # Header files are not checked here because they do not necessarily
            # need to be present in a build file.
            continue

        if str(path) in EXCLUDED_FILES:
            # List of files that don't need to be checked (e.g. examples).
            continue

        project = path.parts[0]
        if not (Path(project) / "BUILD.gn").exists():
            # This project does not use gn.
            # We are trying to be conservative here, as we don't want to check
            # projects that only uses gn in a subdirectory.
            continue
        if path not in project_literals.GetLiterals(project):
            print(
                f"{__file__}: {path} is not included in any "
                f"*.gn files in {project}. "
                "If you have added the file via an intermediate variable, "
                "please ensure the source is set via source_set().",
                file=sys.stderr,
            )
            ret = False

    return ret


def CheckBuildFileIncludingAllSourceFiles(
    commit: str, file_paths: Iterable[str]
) -> bool:
    """Check that BUILD.gn files including all source files.

    Args:
        commit: The commit to check in.
        file_paths: Files modified in this commit. Non-build files in this list
                    would be ignored.
    """
    ret = True
    project_literals = ProjectLiterals(commit)

    # Calling CheckSourceFileIncludedInBuild with all source files in that
    # project as parameter to examine if all source files are included.
    for path in file_paths:
        if not path.endswith(".gn"):
            continue

        project = PurePath(path).parts[0]
        if not CheckSourceFileIncludedInBuild(
            commit,
            (project / f for f in project_literals.FindSourceFiles(project)),
        ):
            ret = False

    return ret


def get_parser():
    """Return an argument parser."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", help="Hash of commit to check in.")
    parser.add_argument("files", nargs="*", help="Files to check.")
    return parser


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    parser = get_parser()
    opts = parser.parse_args(argv)
    return (
        0
        if (
            CheckSourceFileIncludedInBuild(opts.commit, opts.files)
            and CheckBuildFileIncludingAllSourceFiles(opts.commit, opts.files)
        )
        else 1
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
