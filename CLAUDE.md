# Claude Buddy — Project Notes

## TFT Display: Never call fillScreen in continuous animations

`fillScreen` writes 64,800 bytes (240×135×2) over SPI. At 40 MHz SPI that is
~13 ms. The main loop runs at 16 ms (`FRAME_PACING_MS`). Calling `fillScreen`
on every frame leaves the display black for ~80% of each frame, causing visible
strobing on static elements.

**Rule:** any animation that redraws every tick (i.e. its dirty-check value
changes every loop iteration) must erase only the bounding box of the pixels
that actually change, not the full screen.

**Safe pattern used in `eyes_render`:**
- State transitions (`full_clear=true`): `fillScreen` once to clear the
  previous state's pixels.
- Incremental animation frames (`full_clear=false`): `fillRect` over just the
  animated region (~2 100 px, ~1 ms).

States that repaint infrequently (IDLE blinks every 8+ s) can still use
`fillScreen` because a single 13 ms flash at multi-second intervals is
imperceptible.

## Agent skills

### Issue tracker

Issues live as markdown files under `.scratch/<feature-slug>/` in this repo. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary (needs-triage, needs-info, ready-for-agent, ready-for-human, wontfix). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout — `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
