# -*- coding: utf-8 -*-
"""Movie updater scaffold for rsfgraph."""

from .base import MovieUpdater


class GraphMovieUpdater(MovieUpdater):
    mode = "graph"
