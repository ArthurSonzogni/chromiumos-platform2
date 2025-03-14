#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

from datetime import datetime
from pathlib import Path
import shutil
import subprocess
from typing import Any, Callable, cast, Final, Iterable, Literal, TypeVar

import fpsutils
import jinja2
import plotly.graph_objects as go
import plotly.offline
import requests


ElementType = TypeVar("ElementType", bound="Element")


class Element:
    """A `Element` is the organizational unit that helps compose a `Report`.

    `Element` is the base class for all dynamic elements of a report.
    A Report is a tree of `Element`s such that the root `Element`'s `parent`
    is `None`.
    """

    ID_SEPARATOR: Final[str] = "/"

    _id: str
    _path: Path | None
    _parent: Element | None
    _children: dict[str, Element]

    def __init__(
        self,
        id: str,
        #  path: Path | None = None,
        parent: Element | None = None,
    ) -> None:
        """Construct an `Element`.

        Args:
            `id` is a component of a hierarchical identifier/file-path that
            will be used to compose file names and paths in conjunction with
            labels. Only the concatenation of all full depth nodes needs to be
            unique.

            `path` is the path component that will be contributed to the overall
            path of other Elements.

            `parent` is a reverse pointer to the parent `Element`. None for
            the root `Element` or an unassigned child.
            This will be overridden by `_child_add`.
        """
        self._id = id
        self._parent = parent
        # Starting in Python 3.7, dicts maintain insert order.
        self._children: dict[str, Element] = {}
        # self._path = path

    def name(self) -> str:
        """Get the name of the current `Element`."""
        return self._id

    def names(self) -> list[str]:
        """Compose an ordered list of names from root parent to self."""
        if not self._parent:
            return [self.name()]
        return self._parent.names() + [self.name()]

    def id(self, sep: str = ID_SEPARATOR, trim_front=1) -> str:
        """Compose the full ID by joining parent names with `sep`."""
        return sep.join(self.names())

    def display(self, display_fn: Callable[[Any], Any], level: int = 0):
        """Display current element and all children elements."""

        spacing = " ".join([""] * level * 2)
        display_fn(f"{spacing}{type(self).__name__}[{self.name()}]")
        for c in self._children.values():
            c.display(display_fn, level + 1)

    # def path(self) -> Path:
    # Compose path pieces of parent nodes, but skip None `path`s of nodes.
    # ...

    def _child_find(self, id: str, sep: str = ID_SEPARATOR) -> Element | None:
        parts = id.split(sep, 1)
        if len(parts) == 0:
            return None

        child = self._children.get(parts[0], None)
        if len(parts) == 1:
            return child

        if not child:
            return None

        return child._child_find(parts[1], sep)

    def _child_list(self) -> Iterable[Element]:
        """List added children."""
        return self._children.values()

    def _child_add(self, child: ElementType) -> ElementType:
        """Add child Element to children list.

        We allow children to overwrite existing children to allow for iterating
        a design in Jupyter notebook.
        """
        child._parent = self
        self._children[child.name()] = child
        return child


class Text(Element):
    """Represents an embedded text blob with a description."""

    def __init__(self, name: str, parent: Element | None = None) -> None:
        super().__init__(name, parent)
        self._text_blob = ""

    def append(self, text: str) -> None:
        self._text_blob += text

    def text(self) -> str:
        return self._text_blob

    def display(self, display_fn: Callable[[Any], Any], level: int = 0):
        """Display current element and all children elements."""

        spacing = " ".join([""] * level * 2)
        display_fn(f"{spacing}{type(self).__name__}[{self.name()}]")
        display_fn(f"{spacing}`-{self.text()}")


class Data(Element):
    """Represents key/value data."""

    UNSET_VALUE: Final[str] = ""

    def __init__(self, name: str, parent: Element | None = None) -> None:
        super().__init__(name, parent)
        self._data: dict[str, Any] = {}

    def set(self, key: str, value: Any) -> None:
        self._data[key] = value

    def get(self, key: str) -> Any:
        return self._data.get(key, Data.UNSET_VALUE)

    def display(self, display_fn: Callable[[Any], Any], level: int = 0):
        """Display current element and all children elements."""

        spacing = " ".join([""] * level * 2)
        display_fn(f"{spacing}{type(self).__name__}[{self.name()}]")
        for k, v in self._data.items():
            display_fn(f"{spacing}`-{k}: {v}")


class Figure(Element):
    """Represents an embedded figure with a description."""

    def __init__(
        self,
        name: str,
        description: str,
        figure: go.Figure,
        parent: Element | None = None,
    ) -> None:
        """Represents an embedded figure with a description.

        You can provide a copy of the figure by calling `go.Figure(figure)`
        before passing to this constructor.
        """
        super().__init__(name, parent)
        self._description = description
        self._figure = figure

    def description(self) -> str:
        return self._description

    def figure(self) -> go.Figure:
        return self._figure

    def image(self, type: Literal["svg", "pdf", "html"]) -> Path:
        names = self.names()
        full_path = Path("/".join(names) + "." + type)
        full_path.parent.mkdir(parents=True, exist_ok=True)
        if type == "html":
            self._figure.write_html(full_path)
        else:
            self._figure.write_image(full_path)

        # TODO: Fix this hack for specific path structure.
        in_doc_path = Path("/".join(names[1:]) + "." + type)
        return in_doc_path

    def html(self) -> str:
        # https://plotly.com/python-api-reference/generated/plotly.io.to_html.html
        return self._figure.to_html(
            # include_plotlyjs='directory',
            include_plotlyjs=False,  # We include in html template once.
            full_html=False,
            # default_width='400px',
            # default_height='300px',
        )

    def display(self, display_fn: Callable[[Any], Any], level: int = 0):
        """Display current element and all children elements."""

        spacing = " ".join([""] * level * 2)
        display_fn(f"{spacing}{type(self).__name__}[{self.name()}]")
        display_fn(self._figure)


class Section(Element):
    """A `Section` is the organizational unit that helps composes a `Report`.

    It may contains sub `Section`s and `Item`s. The root `Section` shall have
    `None` as the `parent`.
    """

    def __init__(
        self,
        name: str,
        title: str | None = None,
        description: str | None = None,
        parent: Section | None = None,
    ) -> None:
        super().__init__(name, parent)
        self._title = title
        self._description = description

    def title(self) -> str | None:
        return self._title

    def description(self) -> str | None:
        return self._description

    def add_subsection(
        self,
        name: str,
        title: str | None = None,
        description: str | None = None,
    ) -> Section:
        return self._child_add(Section(name, title, description))

    def add_text(self, name: str) -> Text:
        return self._child_add(Text(name))

    def add_data(self, name: str) -> Data:
        return self._child_add(Data(name))

    def add_figure(
        self, name: str, description: str, figure: go.Figure
    ) -> Figure:
        return self._child_add(Figure(name, description, figure))

    def children(self) -> Iterable[Element]:
        return self._child_list()

    def find(self, id: str) -> Element | None:
        return self._child_find(id)

    # def dir(self) -> Path:
    #     """Provide the path to the test case directory."""
    #     return self._rpt.dir() / self._name

    # def relative_dir(self) -> Path:
    #     """Provide the directory path, relative to the report directory."""
    #     return Path(self._name)

    def display(self, display_fn: Callable[[Any], Any], level: int = 0):
        """Display current element and all children elements."""

        spacing = " ".join([""] * level * 2)
        display_fn(f"{spacing}{type(self).__name__}[{self.name()}]")
        display_fn(f"{spacing}`-{self.description()}")
        for c in self._children.values():
            c.display(display_fn, level + 1)


class Report:
    def __init__(
        self,
        out_dir_path: Path,
        template_dir_path: Path = Path("templates"),
        name: str = "Unnamed Evaluation",
    ):
        self._name = name

        self._path_out_dir = out_dir_path
        self._path_out_dir.mkdir(parents=True, exist_ok=True)

        # Check that the necessary templates exist.
        self._template_dir_path = template_dir_path
        self._path_report_template_html = (
            template_dir_path / "index.html.jinja2"
        )
        self._path_assets = template_dir_path / "assets"

        if not self._template_dir_path.is_dir():
            raise Exception(
                f"Report templates {self._template_dir_path} "
                "dir does not exist."
            )
        if not self._path_report_template_html.exists():
            raise Exception(
                f"Report template {self._path_report_template_html} "
                "does not exist."
            )
        if not self._path_assets.is_dir():
            raise Exception(
                f"Report assets {self._path_assets} dir does not exist."
            )

        self._root = Section(
            str(out_dir_path),
            "",
        )
        self._test_cases = self._root.add_subsection("TestCases")
        self._overall = self._root.add_subsection("Overall")
        info = self._overall.add_data("info")
        info.set("name", self._name)
        info.set("title", f"Fingerprint Performance Analysis for {self._name}")
        info.set(
            "description",
            f"Fingerprint performance analysis for the {self._name}.",
        )

    def write_plotlyjs(self, out_dir: Path) -> None:
        # https://plotly.com/python-api-reference/generated/plotly.io.to_html.html
        min_js = plotly.offline.get_plotlyjs()
        (out_dir / "plotly.min.js").write_text(min_js, "utf-8")
        # write to plotly.min.js in html directory
        # use the | full_html=False, include_plotlyjs='directory' | options

    def download_asset(self, url: str, out_file: Path) -> None:
        """Download an asset using a simple GET request."""
        resp = requests.get(url, allow_redirects=True)
        resp.raise_for_status()
        out_file.write_bytes(resp.content)

    def test_case_add(self, name: str, description: str) -> Section:
        return self._test_cases.add_subsection(name, description=description)

    def test_case_list(self) -> Iterable[Section]:
        children = self._test_cases.children()
        assert all(isinstance(e, Section) for e in children)
        return cast(Iterable[Section], children)

    def overall_section(self) -> Section:
        return self._overall

    def _generate_render_context(self) -> dict[str, object]:
        date_str = datetime.now().strftime("%B %d, %Y %H:%M:%S")

        d: dict[str, object] = {
            "date": date_str,
            "test_cases": self._test_cases,
            "overall": self._overall,
        }

        return d

    def generate(self):
        """Generate an HTML report with all findings."""
        jinja_env = jinja2.Environment(
            loader=jinja2.FileSystemLoader(self._template_dir_path),
            undefined=jinja2.StrictUndefined,
        )

        jinja_env.filters["fmt_far"] = fpsutils.fmt_far
        jinja_env.filters["fmt_frr"] = fpsutils.fmt_frr

        ctx = self._generate_render_context()

        report_template_html = jinja_env.get_template("index.html.jinja2")
        report_html_str = report_template_html.render(ctx)
        path_asset_out = self._path_out_dir / self._path_assets.name
        path_asset_out.mkdir(parents=True, exist_ok=True)
        self.write_plotlyjs(path_asset_out)
        self.download_asset(
            "https://raw.githubusercontent.com/andybrewer/mvp/v1.15/mvp.css",
            path_asset_out / "mvp.css",
        )
        shutil.copytree(self._path_assets, path_asset_out, dirs_exist_ok=True)
        (self._path_out_dir / "index.html").write_text(report_html_str, "utf-8")
