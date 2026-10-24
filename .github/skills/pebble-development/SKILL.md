---
name: pebble-development
description: Build, review, and test Roto360 reimaged, a C Pebble watchface with a rotating hour and minute dial. Use for lifecycle, Health, seconds modes, offline PebbleKit JS settings, dial layout, the rotated digit renderer, fonts or atlas, supported watches, store assets, or SDK build instructions. Keep emulator checks separate from physical-watch behavior.
compatibility: Node.js 22 or later, GCC or Clang, Python 3 with Pillow for the tools, Pebble CLI 5.0.40 and SDK 4.33.1. Use WSL on Windows.
---

# Pebble development

## Read the relevant source and live docs

Start with `README.md`, `app/package.json`, and `.github/workflows/tests.yml`. This is a C watchface with a small PebbleKit JS companion providing offline settings for seconds mode and daily step goal. Keep Health data on the watch; do not add network access, cloud services, or Alloy without an owner decision.

Fetch only the official pages needed for the change. Use their Markdown versions instead of copying a documentation snapshot:

- [Agent guidance](https://developer.repebble.com/guides/ai-agents.md) and [documentation index](https://developer.repebble.com/llms.txt).
- [Building for every Pebble](https://developer.repebble.com/guides/best-practices/building-for-every-pebble.md).
- [Graphics and drawing](https://developer.repebble.com/guides/graphics-and-animations/drawing-primitives-images-and-text.md) and [framebuffer access](https://developer.repebble.com/guides/graphics-and-animations/framebuffer-graphics.md).
- [Event services](https://developer.repebble.com/guides/events-and-services/events.md) for tick and battery updates.
- [Battery life](https://developer.repebble.com/guides/best-practices/conserving-battery-life.md) and [Health](https://developer.repebble.com/guides/events-and-services/health.md) for seconds wakeups, on-watch steps, availability and service events.
- [SDK setup](https://developer.repebble.com/sdk.md).

Pebble's [upstream watchface agent skill](https://github.com/coredevices/pebble-watchface-agent-skill/blob/main/skills/pebble-watchface/SKILL.md) is an optional reference for the build, emulator, and screenshot loop, not a vendored dependency. These instructions are project-owned. Where it defaults to `emery`, this project treats `gabbro` (Pebble Round 2, 260x260) as the primary target. Do not run upstream generators, replace the existing C/PebbleKit JS architecture, generate a new UUID, or narrow the target list. Its minute-tick default does not remove this face's intentional configurable seconds display.

## Preserve the app's boundaries

- Keep the targets `basalt`, `chalk`, `emery`, and `gabbro`. Round screens draw circular rings; rectangular screens draw a rounded-square bezel through the same `DialTrack` geometry. Check both shapes after layout changes.
- Keep pure logic in `app/src/c/dial.c` with no SDK includes so the host tests can build it.
- Draw rotated numerals through `numerals.c` and the generated atlas, not runtime bitmap scaling. After changing `tools/gen_digits.py` or the font, regenerate `app/resources/data/digits.bin` and confirm `python3 tools/gen_digits.py --check` passes.
- Use only original code and art, or assets with a compatible license recorded in `NOTICE`. Do not copy, trace, or extract files from the original Roto360.
- Keep the app UUID unchanged so updates replace earlier builds of this face, never the original.

## Review runtime changes

- Match service subscriptions with teardown, cancel the glance timer on focus loss and shutdown, and preserve minute ticks when seconds are hidden. Keep Off, Always and shake mode, the ten-second timeout, and legacy persisted settings compatible; do not silently change the default.
- Check allocation, resource-load, timer and AppMessage results. Report failures rather than leaving a success-looking face with missing time digits. Avoid adding per-frame allocations or continuous timer loops; profile rendering before proposing caching or partial redraws.
- Capture the framebuffer only inside a layer update and release it before SDK drawing resumes. Keep `gbitmap_get_data_row_info()` and its row limits for both Chalk and Gabbro; do not assume circular framebuffers have a fixed or shared stride.
- Keep Health availability distinct from zero steps, gate Health APIs with `PBL_HEALTH`, and preserve settings message keys and persistence keys. Validate both settings before applying them, and report companion storage or delivery failures.

## Verify the changed behavior

Run `npm test` from the root. It builds the dial logic natively with GCC or Clang and checks resources and store assets.

For app, resource, or build changes, run from `app`:

```text
pebble build
pebble install --emulator gabbro
pebble emu-set-time --emulator gabbro 1794126000
pebble screenshot --no-open --emulator gabbro screenshot.png
```

Use a temporary output location for screenshots. Cover 12-hour and 24-hour modes (`pebble emu-time-format`), low, full and charging battery (`pebble emu-battery`), and the 11:59, 23:59 and midnight rollovers. Check that numerals stay legible and are not clipped at the screen edge. One Round 2 capture does not cover the other watches.

In a headless environment, add `--vnc` to emulator commands. Confirm the screenshot actually shows this face, not a stale app or crash screen. State exactly which checks ran: host tests do not execute the SDK lifecycle or framebuffer renderer, compilation is not UI verification, and emulator captures do not measure real-watch power or prove mobile settings compatibility.

For step progress, check zero, partial, goal and over-goal totals (`pebble emu-steps`), goal configuration and restart persistence, the midnight reset, and the unavailable-Health indicator. Progress belongs on the outer bezel, separate from the inner seconds marker. Cover seconds Off, Always and shake mode (`pebble emu-tap`), including the ten-second timeout, re-triggering and focus loss. Off and idle shake mode must use minute ticks rather than second ticks with an invisible marker.

Regenerate the store set with `python3 tools/make_store_assets.py store/screenshots store` after replacing screenshots. Screenshots must match each display's size exactly.

Do not install on a physical watch, publish to the store, change authentication, reset emulator data, or rewrite Git history as an automatic cleanup step. Obtain explicit authorization for those actions. Reuse an authorized SDK profile without reading its secrets.
