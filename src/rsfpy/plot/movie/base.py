# -*- coding: utf-8 -*-
"""Base classes for efficient SVG movie updates."""

from dataclasses import dataclass, field
from typing import Any, Dict, Optional, Union


@dataclass
class MovieFrame:
    index: int
    label: Optional[str] = None
    payload: Any = None
    clip: Optional[float] = None
    bias: float = 0.0
    allpos: bool = False
    cmap: str = "gray"
    dpi: float = 100.0
    pclip: Optional[float] = None
    frame1: int = 0
    frame2: int = 0
    frame3: int = 0
    point1: float = 0.8
    point2: float = 0.4
    gain: Any = None


@dataclass
class MovieTemplate:
    svg: Union[bytes, str]
    mode: str
    metadata: Dict[str, Any] = field(default_factory=dict)


class MovieUpdater:
    """Interface for updating an SVG template without full matplotlib redraw."""

    mode = "base"

    def build_template(self, figure, context=None):
        raise NotImplementedError

    def update_frame(self, template, frame, context=None):
        raise NotImplementedError

    def render_frame(self, template, frame, context=None):
        updated = self.update_frame(template, frame, context=context)
        return updated.svg
