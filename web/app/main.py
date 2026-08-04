import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Annotated

from fastapi import FastAPI, Form, HTTPException, Request
from fastapi.responses import RedirectResponse
from fastapi.templating import Jinja2Templates

from . import settings
from .pump import BrokerUnavailableError, PumpState, read_state, set_angle

log = logging.getLogger(__name__)

app = FastAPI()
templates = Jinja2Templates(directory=Path(__file__).parent / "templates")


@dataclass(frozen=True)
class Preset:
    angle: int
    label: str


# The only two positions the knob is ever put in. The full 0..180 servo range is
# not the knob's usable travel and has never been calibrated, so there is
# deliberately no slider or free-form angle here.
PRESETS = (
    Preset(angle=150, label="20 vaille"),
    Preset(angle=30, label="5 vaille"),
)

settings.require_config()


def _label_for(angle: int | None) -> str | None:
    return next((p.label for p in PRESETS if p.angle == angle), None)


def _context(state: PumpState, requested: int | None, error: str | None) -> dict:
    return {
        "presets": PRESETS,
        "angle": state.angle,
        "angle_label": _label_for(state.angle),
        "online": state.online,
        # Only worth mentioning while the knob hasn't caught up. Once the state
        # matches, the page shows the real thing and the caveat is just noise.
        "requested": requested if requested != state.angle else None,
        "requested_label": _label_for(requested),
        "error": error,
    }


@app.get("/")
async def index(request: Request, requested: int | None = None):
    error = None
    state = PumpState(angle=None, online=None)
    try:
        state = await read_state()
    except BrokerUnavailableError as exc:
        # A dead broker is a status to render, not a 500. The page still needs
        # to explain itself and it's the only thing the user can see.
        log.warning("Could not read state: %s", exc)
        error = "Yhteys ei onnistunut. Tila ei tiedossa."

    return templates.TemplateResponse(
        request,
        "index.html",
        _context(state, requested, error),
    )


@app.post("/set")
async def set_pump(angle: Annotated[int, Form()]):
    if angle not in {p.angle for p in PRESETS}:
        raise HTTPException(status_code=400, detail="Unknown preset")

    try:
        echoed = await set_angle(angle)
    except BrokerUnavailableError as exc:
        log.warning("Could not publish angle %d: %s", angle, exc)
        raise HTTPException(status_code=502, detail="Broker unavailable") from exc

    # Carry the commanded angle through the redirect when the firmware hasn't
    # echoed yet, so the page can say "requested" instead of rendering the old
    # position and looking like nothing happened.
    target = "/" if echoed is not None else f"/?requested={angle}"
    return RedirectResponse(target, status_code=303)


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8001)  # noqa: S104
