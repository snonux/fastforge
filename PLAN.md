**FastForge – Pebble Intermittent Fasting Tracker**  
**Complete Development Plan & Implementation Guide**  
**April 2026 Edition (Rebble SDK 4.9+ – Pure C API)**  

**App Name:** FastForge  
**Tagline:** Forge your fasting habit with science, streaks, and smart visuals.  

---

### 1. App Purpose
A lightweight, native Pebble watchapp that turns your watch into a complete intermittent-fasting coach:  
- Real-time timer + countdown  
- Alarm when you hit your fasting goal  
- Full history with editing  
- Motivational stats & streaks  
- Live fasting-stage visuals (backed by 2024–2026 metabolic science)  
- Educational protocol comparison  
- Phone backup support  

Everything runs locally on your Linux laptop using the official Rebble SDK. No CloudPebble required.

---

### 2. Full Feature List

| Priority | Feature | Description |
|----------|---------|-------------|
| Must | Start / Stop Fast | One-tap start with optional preset target |
| Must | Live Timer + Countdown | Big elapsed time; switches to countdown if target set |
| Must | Edit Running Fast | Adjust start time on-the-fly |
| Must | Edit Past Fasts | Change start/end times of completed entries |
| Must | Threshold Alarm | Strong custom vibration + full-screen “Goal Reached!” window |
| Must | Persistent History | Up to 64 fasts stored forever |
| Must | Phone Backup | AppMessage export (CSV/JSON) to Android/iOS companion |
| High | Quick-Start Presets | 16 h, 18 h, 20 h, 24 h, 36 h |
| High | Visual Progress | Bar or arc in timer view |
| High | Live Fasting Stage Indicator | GLYCOGEN → FAT BURN ↑ → EARLY KETOSIS 🔥 → DEEP KETOSIS (ticks at 12/18/24 h) |
| High | Fasting Streak Counter | Current + longest streak on every screen |
| High | Statistics Dashboard | Avg length, total hours, success rate, longest fast |
| Medium | Notes/Tags on Fast | Quick tags + auto milestone badges (Fat Burn, Ketosis, etc.) |
| ~~Medium~~ | ~~Fasting Science Screen~~ | ~~Physiology timeline + 5×20 h vs 2×24 h + 2×18 h comparison~~ (removed) |
| Debug | Developer Mode | Fast-forward time, force alarm, fake clock (for local testing) |

---

### 3. Data Model (`src/c/fastforge.h`)

```c
#include <pebble.h>

typedef struct {
    time_t start_time;
    time_t end_time;           // 0 = still running
    uint16_t target_minutes;
    char note[32];             // e.g. "felt amazing", "workout day"
    uint8_t max_stage_reached; // 0=none, 1=12h, 2=18h, 3=24h+
} FastEntry;

#define MAX_FASTS 64
#define KEY_HISTORY_COUNT   1
#define KEY_HISTORY_DATA    2
#define KEY_CURRENT_FAST    3
#define KEY_TARGET_MIN      4
#define KEY_STREAK_DATA     5

static FastEntry history[MAX_FASTS];
static int history_count = 0;
static FastEntry current_fast = {0};
static uint16_t global_target_minutes = 16 * 60; // default 16 h
static AppTimer *alarm_timer = NULL;
static time_t target_time = 0;
```

---

### 4. Persistent Storage (on launch / any change)

```c
static void save_all_data(void) {
    persist_write_int(KEY_HISTORY_COUNT, history_count);
    persist_write_data(KEY_HISTORY_DATA, history, sizeof(FastEntry) * history_count);
    persist_write_data(KEY_CURRENT_FAST, &current_fast, sizeof(FastEntry));
    persist_write_int(KEY_TARGET_MIN, global_target_minutes);
    // streak data saved similarly
}

static void load_all_data(void) {
    if (persist_exists(KEY_HISTORY_COUNT))
        history_count = persist_read_int(KEY_HISTORY_COUNT);
    if (history_count > 0)
        persist_read_data(KEY_HISTORY_DATA, history, sizeof(FastEntry) * history_count);
    persist_read_data(KEY_CURRENT_FAST, &current_fast, sizeof(FastEntry));
    if (persist_exists(KEY_TARGET_MIN))
        global_target_minutes = persist_read_int(KEY_TARGET_MIN);
}
```

---

### 5. Alarm Implementation

```c
static const uint32_t alarm_vibe[] = {200, 100, 200, 100, 400, 100, 200};
static VibePattern alarm_pattern = { .durations = alarm_vibe, .num_segments = ARRAY_LENGTH(alarm_vibe) };

static void alarm_callback(void *data) {
    alarm_timer = NULL;
    target_time = 0;
    vibes_enqueue_custom_pattern(alarm_pattern);
    light_enable_interaction();
    show_goal_reached_window();
}

static void schedule_alarm_if_needed(void) {
    if (alarm_timer) app_timer_cancel(alarm_timer);
    if (!current_fast.start_time || global_target_minutes == 0) return;

    target_time = current_fast.start_time + (global_target_minutes * 60);
    time_t now = time(NULL);
    if (target_time <= now) {
        alarm_callback(NULL);
        return;
    }
    uint32_t ms = (target_time - now) * 1000;
    alarm_timer = app_timer_register(ms, alarm_callback, NULL);
}
```

---

### 6. Timer Window with Visuals (Progress Bar + Stage Indicator)

```c
static void timer_update_proc(Layer *layer, GContext *ctx) {
    time_t elapsed_sec = time(NULL) - current_fast.start_time;
    uint32_t total_sec = global_target_minutes * 60;

    // Background bar
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(10, 120, 124, 12), 2, GCornersAll);

    // Progress
    int progress = (total_sec > 0) ? (elapsed_sec * 124) / total_sec : 0;
    graphics_context_set_fill_color(ctx, GColorGreen);
    graphics_fill_rect(ctx, GRect(10, 120, progress, 12), 2, GCornersAll);

    // Stage ticks
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_draw_line(ctx, GPoint(10 + (124/3), 118), GPoint(10 + (124/3), 134)); // ~12 h
    graphics_draw_line(ctx, GPoint(10 + (2*124/3), 118), GPoint(10 + (2*124/3), 134)); // ~24 h

    // Current stage text
    const char* stage = (elapsed_sec < 12*3600) ? "GLYCOGEN" :
                        (elapsed_sec < 18*3600) ? "FAT BURN ↑" : "KETOSIS 🔥";
    // Draw stage text with TextLayer or GContext
}
```

---

### 7. ~~Fasting Science Screen~~ (Removed)

This feature was removed. The physiology timeline and protocol comparison screen has been deleted from the app.

---

### 8. Best Practices for Pebble C SDK (2026)

- Always `#include <pebble.h>`
- Use `Window`, `SimpleMenuLayer`, `TextLayer`, `GraphicsLayer` only
- Keep `update_proc` fast (< 10 ms)
- Save data on every change + on app exit (`window_set_window_handlers` unload)
- `#ifdef DEBUG` for fast-forward / fake time helpers
- Cancel timers on window unload / app deinit
- Use `persist_*` / `storage_*` for all data
- Keep total RAM under ~8 KB (history array is ~4 KB)
- Test with `pebble logs` and emulator constantly

---

### 9. Local Linux Development & Testing Workflow

```bash
# One-time setup (2026)
uv tool install pebble-tool --python 3.13
pebble sdk install latest

# Daily cycle
pebble build
pebble install --emulator basalt
pebble logs          # second terminal
```

**Debug helpers** (compile only with `DEBUG=1`):  
- Fast-forward X hours  
- Force alarm now  
- Set fake current time

---

### 10. Phone Backup (AppMessage)

Define a simple dictionary with `app_message_open()` and one command “EXPORT_HISTORY”.  
On phone side (minimal companion app or Rebble tools) receive the data as CSV.

---

### 11. Main Menu Structure (SimpleMenuLayer)

1. Start New Fast → Presets submenu  
2. Current Timer  
3. Stop Current Fast (if running)  
4. History  
5. Statistics  
6. Settings  
7. Backup to Phone  
8. About  

---

### 12. Development Roadmap (1-week plan)

**Day 1:** Project setup + data model + persistence + basic start/stop  
**Day 2:** Timer window + visuals (progress + stage) + alarm  
**Day 3:** History + edit screens + notes/tags  
**Day 4:** Statistics + streak + presets  
**Day 5:** ~~Fasting Science screen + protocol comparison~~ (feature removed)  
**Day 6:** Settings + AppMessage backup stub  
**Day 7:** Polish, debug menu, final testing on emulator  

