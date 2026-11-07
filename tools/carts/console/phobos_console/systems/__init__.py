"""The system registry.

``SYSTEMS`` is the one table the CLI reads: each row names the root node, the
committed GLB path, the palette, the builder, the preview prefix/resolution and
the stage kind.  Adding a system is one module here plus one row in this table.

The builder signature is ``build(mats, stage)``, where ``stage`` is the
callable looked up from ``STAGES`` for the row's stage kind, or ``None``.  A
``diorama`` system gets the shared CRT TV; a future ``handheld`` system gets no
set piece and simply skips it.
"""
from ..crt import add_crt
from . import nes, snes

#: Stage kinds.  A stage callable places the shared set piece; ``None`` means
#: the system has no stage (a handheld lying on the same studio floor).
STAGES = {
    "diorama": add_crt,
    "handheld": None,
}

SYSTEMS = {
    "snes": {
        "root": snes.ROOT,
        "output": snes.OUTPUT,
        "palette": snes.PALETTE,
        "builder": snes.build,
        "preview_prefix": snes.PREVIEW_PREFIX,
        "preview_res": snes.PREVIEW_RES,
        "stage": snes.STAGE,
    },
    "nes": {
        "root": nes.ROOT,
        "output": nes.OUTPUT,
        "palette": nes.PALETTE,
        "builder": nes.build,
        "preview_prefix": nes.PREVIEW_PREFIX,
        "preview_res": nes.PREVIEW_RES,
        "stage": nes.STAGE,
    },
}
