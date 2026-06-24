# -*- coding: utf-8 -*-
"""Movie updater scaffold for rsfwiggle."""

from .base import MovieUpdater


class WiggleMovieUpdater(MovieUpdater):
    mode = "wiggle"
