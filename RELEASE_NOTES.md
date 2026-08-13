# FastForge Release Notes

## v1.4.0 — August 2026

State-aware main menu and large-font readability improvements for Pebble
Time 2 (emery) and Pebble Round 2 (gabbro).

### Features
- **State-aware main menu:** the menu now only shows actions relevant to the
  current state. Start New Fast and Resume Last Fast appear only when no fast
  is running; Current Timer, Stop Current Fast, and Cancel Current Fast appear
  only while a fast is active. No more dead-end "not running" placeholder
  screens. History, Statistics, Settings, and About stay visible in every state.
- **Functional colour:** stage coding and per-statistic accent colours on
  colour platforms, with clean high-contrast fallbacks on Aplite.
- **Paged statistics:** large per-statistic sub-screens with the same hero-font
  treatment as the timer.
- **Minimum-fast-time setting:** choose how short a fast may be and still be
  saved to history (default 10 min), so accidental taps don't clutter the log.
- **Ask before saving:** when you stop a fast, FastForge now confirms whether to
  save it to history or discard it.
- **Hero fasting time on stop-confirm:** the stopped fast's elapsed time is
  shown in the large hero font.
- **Hero elapsed counter:** the timer's page-1 elapsed counter is now hero-sized
  like the main countdown.

### Fixes
- About screen renders the source URL cleanly on a dedicated screen instead of
  wrapping into a broken three-line mess.

### Notes
- Build: `pebble build` produces `build/fastforge.pbw` for both emery and
  gabbro. This is a non-debug release build.