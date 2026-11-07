"""The system registry, discovered from the modules beside this file.

One ``systems/<name>.py`` module is one system.  This module imports every such
module, checks its metadata and builds ``SYSTEMS`` from it; nothing else
registers a system, so two authors adding two consoles write two different files
and never edit a shared one.

A system module provides:

``ROOT``             the exported root node name
``OUTPUT``           the committed GLB path (repository-relative)
``PALETTE``          the authored linear-RGB palette for ``make_materials``
``PREVIEW_PREFIX``   the previews' filename prefix (``""`` gives ``front.png``)
``PREVIEW_RES``      the preview resolution, e.g. ``(1000, 765)``
``STAGE``            a key of ``STAGES``: the shared set piece, or ``None``
``build(mats, stage)``  places the console, calls ``stage(mats)`` if there is a
                     stage, then places the controller and cable, and returns
                     its ``ROOT`` name

Modules whose name starts with ``_`` are private helpers, not systems.  A
system's key in ``SYSTEMS`` is its module name, so ``--system n64`` means
``systems/n64.py``.
"""
import importlib
import pkgutil

from ..crt import add_crt

#: Stage kinds.  A stage callable places the shared set piece between the
#: console and the controller; ``None`` means the system has no stage (a
#: handheld lying on the same studio floor).
STAGES = {
    "diorama": add_crt,
    "handheld": None,
}

#: The attributes every system module has to provide.
_REQUIRED = ("ROOT", "OUTPUT", "PALETTE", "PREVIEW_PREFIX", "PREVIEW_RES",
             "STAGE")


def _load(name):
    module = importlib.import_module(f"{__name__}.{name}")
    missing = [attr for attr in _REQUIRED if not hasattr(module, attr)]
    if missing:
        raise RuntimeError(f"{__name__}.{name}: missing {', '.join(missing)}")
    if not callable(getattr(module, "build", None)):
        raise RuntimeError(f"{__name__}.{name}: no build(mats, stage) function")
    if module.STAGE not in STAGES:
        raise RuntimeError(
            f"{__name__}.{name}: unknown stage {module.STAGE!r}; expected one "
            f"of {', '.join(sorted(STAGES))}")
    return module


def discover():
    """Return ``(systems, modules)`` for every system module, by name.

    Modules are visited in sorted order, so the result does not depend on the
    file system's listing order.
    """
    systems, modules = {}, {}
    for info in sorted(pkgutil.iter_modules(__path__), key=lambda i: i.name):
        if info.name.startswith("_"):
            continue
        module = _load(info.name)
        modules[info.name] = module
        systems[info.name] = {
            "root": module.ROOT,
            "output": module.OUTPUT,
            "palette": module.PALETTE,
            "builder": module.build,
            "preview_prefix": module.PREVIEW_PREFIX,
            "preview_res": module.PREVIEW_RES,
            "stage": module.STAGE,
        }
    if not systems:
        raise RuntimeError(f"{__name__}: no system modules found")
    return systems, modules


#: Every system, keyed by its module name.  Each row is the CLI's whole view of
#: a system: root node, committed GLB path, palette, builder, preview prefix and
#: resolution, and the stage kind.
SYSTEMS, MODULES = discover()

__all__ = ["SYSTEMS", "MODULES", "STAGES", "discover"]
