#include "pebble.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define main watchface_main
#ifdef LIFECYCLE_BASELINE
// The original main relied on C's implicit return, which renaming removes.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include ".baseline-main.c"
#pragma GCC diagnostic pop
#else
#include "../../app/src/c/main.c"
#endif
#undef main

struct Layer { GRect bounds; LayerUpdateProc update; bool live; bool root; };
struct Window { Layer root; WindowHandlers handlers; bool live; bool pushed; };
struct AppTimer { AppTimerCallback callback; void *context; bool live; };

static struct {
  bool fail_window, fail_layer, fail_font_null, fail_font_fallback, fail_atlas, fail_timer;
  bool fail_health_subscription, fail_message;
  Window window;
  Layer layer;
  AppTimer timers[8];
  FontInfo custom_font, system_font;
  bool font_live, atlas_live;
  int window_creates, window_destroys, layer_creates, layer_destroys;
  int font_loads, font_unloads, atlas_loads, atlas_unloads, null_calls;
  int service_starts, dirty, timer_count, timer_cancels, event_loops;
  int errors, warnings, fallback_title, fallback_restart;
  bool tick_live, battery_live, focus_live, tap_live, health_live, message_live;
  TimeUnits tick_units;
  TickHandler tick;
  BatteryStateHandler battery;
  AppFocusHandler focus;
  AccelTapHandler tap;
  AppMessageInboxReceived inbox;
  AppMessageInboxDropped dropped;
  bool mode_saved;
  int32_t mode;
  int health_reads;
#ifdef PBL_HEALTH
  HealthEventHandler health;
  void *health_context;
  bool health_available;
  HealthValue steps;
#endif
} f;

static void check(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

static bool valid(const void *pointer) {
  if (!pointer) {
    f.null_calls++;
    return false;
  }
  return true;
}

void fake_app_log(AppLogLevel level, const char *format, ...) {
  char message[256];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (level == APP_LOG_LEVEL_ERROR) f.errors++;
  if (level == APP_LOG_LEVEL_WARNING) f.warnings++;
}

Window *window_create(void) {
  f.window_creates++;
  if (f.fail_window) return NULL;
  f.window.live = true;
  f.window.root = (Layer){ .bounds = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_WIDTH),
                           .live = true, .root = true };
  return &f.window;
}
void window_destroy(Window *window) {
  if (!valid(window)) return;
  check(window->live, "window must be destroyed exactly once");
  check(!f.tick_live && !f.tap_live && !f.focus_live && !f.battery_live
        && !f.health_live && !f.message_live, "unsubscribe all services before window destruction");
  for (int i = 0; i < f.timer_count; i++) {
    check(!f.timers[i].live, "cancel timers before window destruction");
  }
  // A pushed window is synchronously unloaded during destruction.
  if (window->pushed && window->handlers.unload) window->handlers.unload(window);
  check(!f.layer.live, "window unload must release the dial layer");
  window->root.live = false;
  window->live = false;
  f.window_destroys++;
}
void window_set_background_color(Window *window, GColor color) {
  (void)color; valid(window);
}
void window_set_window_handlers(Window *window, WindowHandlers handlers) {
  if (valid(window)) window->handlers = handlers;
}
void window_stack_push(Window *window, bool animated) {
  (void)animated;
  if (!valid(window)) return;
  window->pushed = true;
  if (window->handlers.load) window->handlers.load(window);
}
Layer *window_get_root_layer(const Window *window) {
  return valid(window) ? (Layer *)&window->root : NULL;
}
Layer *layer_create(GRect bounds) {
  f.layer_creates++;
  if (f.fail_layer) return NULL;
  f.layer = (Layer){ .bounds = bounds, .live = true };
  return &f.layer;
}
void layer_destroy(Layer *layer) {
  if (!valid(layer)) return;
  check(layer->live && !layer->root, "destroy only a live owned dial layer");
  layer->live = false;
  f.layer_destroys++;
}
GRect layer_get_bounds(const Layer *layer) {
  return valid(layer) ? layer->bounds : GRect(0, 0, 0, 0);
}
void layer_set_update_proc(Layer *layer, LayerUpdateProc update) {
  if (valid(layer)) layer->update = update;
}
void layer_add_child(Layer *parent, Layer *child) {
  valid(parent); valid(child);
}
void layer_mark_dirty(Layer *layer) {
  if (valid(layer)) {
    check(layer->live, "do not redraw a destroyed layer");
    f.dirty++;
  }
}
GPoint grect_center_point(const GRect *rect) {
  return GPoint(rect->origin.x + rect->size.w / 2, rect->origin.y + rect->size.h / 2);
}
GFont fonts_load_custom_font(ResHandle handle) {
  valid(handle);
  f.font_loads++;
  if (f.fail_font_null) return NULL;
  if (f.fail_font_fallback) return &f.system_font;
  f.font_live = true;
  return &f.custom_font;
}
GFont fonts_get_system_font(const char *key) {
  valid(key);
  return &f.system_font;
}
void fonts_unload_custom_font(GFont font) {
  if (!valid(font)) return;
  check(font == &f.custom_font, "never unload the SDK fallback/system font as a custom font");
  check(f.font_live, "custom font must be unloaded exactly once");
  f.font_live = false;
  f.font_unloads++;
}
ResHandle resource_get_handle(uint32_t resource) {
  (void)resource;
  return &f.custom_font;
}
bool numerals_load(void) {
  f.atlas_loads++;
  f.atlas_live = !f.fail_atlas;
  return f.atlas_live;
}
void numerals_unload(void) {
  // The real atlas unload is explicitly safe after load failure.
  f.atlas_live = false;
  f.atlas_unloads++;
}
int numerals_width(const char *text, int height) {
  return (int)strlen(text) * height / 2;
}
void numerals_draw(GBitmap *bitmap, const char *text, GPoint center, int32_t angle,
                   int height, GColor color) {
  (void)bitmap; (void)text; (void)center; (void)angle; (void)height; (void)color;
}
void tick_timer_service_subscribe(TimeUnits units, TickHandler handler) {
  check(!f.tick_live, "unsubscribe before replacing tick subscription");
  f.tick_live = true; f.tick_units = units; f.tick = handler; f.service_starts++;
}
void tick_timer_service_unsubscribe(void) { f.tick_live = false; f.tick = NULL; }
void battery_state_service_subscribe(BatteryStateHandler handler) {
  f.battery_live = true; f.battery = handler; f.service_starts++;
}
void battery_state_service_unsubscribe(void) { f.battery_live = false; f.battery = NULL; }
BatteryChargeState battery_state_service_peek(void) {
  return (BatteryChargeState){ .charge_percent = 80 };
}
void app_focus_service_subscribe(AppFocusHandler handler) {
  f.focus_live = true; f.focus = handler; f.service_starts++;
}
void app_focus_service_unsubscribe(void) { f.focus_live = false; f.focus = NULL; }
void accel_tap_service_subscribe(AccelTapHandler handler) {
  f.tap_live = true; f.tap = handler; f.service_starts++;
}
void accel_tap_service_unsubscribe(void) { f.tap_live = false; f.tap = NULL; }
AppTimer *app_timer_register(uint32_t milliseconds, AppTimerCallback callback, void *context) {
  check(milliseconds == 10000, "shake timer must last ten seconds");
  if (f.fail_timer) return NULL;
  check(f.timer_count < 8, "timer fake capacity");
  AppTimer *timer = &f.timers[f.timer_count++];
  *timer = (AppTimer){ .callback = callback, .context = context, .live = true };
  return timer;
}
void app_timer_cancel(AppTimer *timer) {
  if (!valid(timer)) return;
  check(timer->live, "cancel only an active timer");
  timer->live = false;
  f.timer_cancels++;
}
static void fire_timer(AppTimer *timer) {
  check(timer->live, "fire only a live timer");
  timer->live = false;
  timer->callback(timer->context);
}
bool persist_exists(int key) { return key == SETTINGS_KEY_SECONDS && f.mode_saved; }
int persist_get_size(int key) { (void)key; return sizeof(int32_t); }
int persist_read_data(int key, void *buffer, size_t size) {
  (void)key; (void)buffer; (void)size; return -1;
}
int32_t persist_read_int(int key) { (void)key; return f.mode; }
int persist_write_int(int key, int32_t value) { (void)key; (void)value; return sizeof(value); }
Tuple *dict_find(const DictionaryIterator *iterator, uint32_t key) {
  return key == MESSAGE_KEY_ShowSeconds ? iterator->seconds : iterator->goal;
}
AppMessageInboxReceived app_message_register_inbox_received(AppMessageInboxReceived handler) {
  AppMessageInboxReceived old = f.inbox; f.inbox = handler; return old;
}
AppMessageInboxDropped app_message_register_inbox_dropped(AppMessageInboxDropped handler) {
  AppMessageInboxDropped old = f.dropped; f.dropped = handler; return old;
}
AppMessageResult app_message_open(uint32_t inbox, uint32_t outbox) {
  check(inbox >= 32 && outbox > 0, "settings buffers must be allocated");
  f.service_starts++;
  f.message_live = !f.fail_message;
  return f.fail_message ? APP_MSG_OUT_OF_MEMORY : APP_MSG_OK;
}
void app_message_deregister_callbacks(void) {
  f.inbox = NULL; f.dropped = NULL; f.message_live = false;
}
void app_event_loop(void) {
  f.event_loops++;
  check(f.window.live && f.window.pushed, "event loop requires a drawable window");
  GContext context = { 0 };
  if (f.window.root.update) f.window.root.update(&f.window.root, &context);
  if (f.layer.live && f.layer.update) f.layer.update(&f.layer, &context);
}
bool clock_is_24h_style(void) { return true; }
int32_t sin_lookup(int32_t angle) {
  return (int32_t)(sin(angle * 6.283185307179586 / TRIG_MAX_ANGLE) * TRIG_MAX_RATIO);
}
int32_t cos_lookup(int32_t angle) { return sin_lookup(angle + TRIG_MAX_ANGLE / 4); }
void graphics_context_set_fill_color(GContext *context, GColor color) { (void)context; (void)color; }
void graphics_context_set_stroke_color(GContext *context, GColor color) { (void)context; (void)color; }
void graphics_context_set_text_color(GContext *context, GColor color) { (void)context; (void)color; }
void graphics_context_set_stroke_width(GContext *context, uint8_t width) { (void)context; (void)width; }
void graphics_context_set_antialiased(GContext *context, bool enabled) { (void)context; (void)enabled; }
void graphics_fill_rect(GContext *context, GRect rect, uint16_t radius, GCornerMask corners) {
  (void)context; (void)rect; (void)radius; (void)corners;
}
void graphics_fill_circle(GContext *context, GPoint point, uint16_t radius) {
  (void)context; (void)point; (void)radius;
}
void graphics_fill_radial(GContext *context, GRect rect, GOvalScaleMode mode,
                          uint16_t inset, int32_t start, int32_t end) {
  (void)context; (void)rect; (void)mode; (void)inset; (void)start; (void)end;
}
void graphics_draw_line(GContext *context, GPoint start, GPoint end) {
  (void)context; (void)start; (void)end;
}
void graphics_draw_text(GContext *context, const char *text, GFont font, GRect rect,
                        GTextOverflowMode overflow, GTextAlignment alignment,
                        GTextAttributes *attributes) {
  (void)context; (void)overflow; (void)alignment; (void)attributes;
  valid(font);
  check(rect.size.w > 0 && rect.size.h > 0, "draw text into a visible rectangle");
  if (strstr(text, "ROTO360 ERROR")) {
    check(font == &f.system_font, "fallback title uses a system font");
    f.fallback_title++;
  }
  if (strstr(text, "RESTART WATCHFACE")) {
    check(font == &f.system_font, "fallback instruction uses a system font");
    f.fallback_restart++;
  }
}
GPath *gpath_create(const GPathInfo *info) { static GPath path; (void)info; return &path; }
void gpath_draw_filled(GContext *context, GPath *path) { (void)context; (void)path; }
void gpath_destroy(GPath *path) { (void)path; }
GBitmap *graphics_capture_frame_buffer(GContext *context) { (void)context; return NULL; }
void graphics_release_frame_buffer(GContext *context, GBitmap *bitmap) {
  (void)context; valid(bitmap);
}
GBitmapFormat gbitmap_get_format(const GBitmap *bitmap) { valid(bitmap); return GBitmapFormat8Bit; }

#ifdef PBL_HEALTH
HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric metric,
                                                               time_t start, time_t end) {
  (void)metric; (void)start; (void)end; f.health_reads++;
  return f.health_available ? HealthServiceAccessibilityMaskAvailable
                            : HealthServiceAccessibilityMaskNotAvailable;
}
HealthValue health_service_sum_today(HealthMetric metric) { (void)metric; return f.steps; }
bool health_service_events_subscribe(HealthEventHandler handler, void *context) {
  f.service_starts++;
  if (f.fail_health_subscription) return false;
  f.health_live = true; f.health = handler; f.health_context = context;
  return true;
}
bool health_service_events_unsubscribe(void) {
  check(f.health_live, "unsubscribe Health only after a successful subscription");
  f.health_live = false; f.health = NULL; return true;
}
time_t time_start_of_today(void) { return time(NULL) - 60; }
#endif

static void start(DialSecondsMode mode) {
  f.mode_saved = true; f.mode = mode;
  prv_init();
  check(f.window.live && f.layer.live && f.font_live && f.atlas_live,
        "successful startup owns all drawing resources");
  check(f.focus && f.battery && f.tick && f.inbox && f.dropped,
        "successful startup subscribes event handlers");
  check(f.tick_units == (mode == DialSecondsAlways ? SECOND_UNIT : MINUTE_UNIT),
        "startup selects seconds only for Always");
  check(f.tap_live == (mode == DialSecondsOnShake), "tap subscription follows seconds mode");
}

static void assert_clean(void) {
  check(f.null_calls == 0, "never call SDK APIs with NULL");
  check(!f.window.live && !f.layer.live && !f.font_live && !f.atlas_live,
        "cleanup balances every successfully allocated resource");
  check(!f.tick_live && !f.battery_live && !f.focus_live && !f.tap_live
        && !f.health_live && !f.message_live && !f.inbox && !f.dropped,
        "cleanup removes services and messaging callbacks");
  check(f.window_destroys == (f.fail_window ? 0 : f.window_creates),
        "release each created window");
  check(f.layer_destroys == (f.fail_layer ? 0 : f.layer_creates),
        "release each created layer");
}

static void startup_failure(const char *scenario) {
  f.fail_window = strcmp(scenario, "window-failure") == 0;
  f.fail_layer = strcmp(scenario, "layer-failure") == 0;
  f.fail_font_null = strcmp(scenario, "font-null") == 0;
  f.fail_font_fallback = strcmp(scenario, "font-fallback") == 0;
  f.fail_atlas = strcmp(scenario, "atlas-failure") == 0;
  watchface_main();
  check(f.service_starts == 0, "failed startup must not start any service");
  check(f.errors > 0, "failed startup is logged");
  check(f.event_loops == (f.fail_window ? 0 : 1), "keep drawable startup errors in the event loop");
  if (!f.fail_window) {
    check(f.fallback_title > 0 && f.fallback_restart > 0,
          "drawable startup failures show ROTO360 ERROR and RESTART WATCHFACE");
  }
  assert_clean();
  prv_deinit();
  assert_clean();
}

int main(int argc, char **argv) {
  check(argc == 2, "pass a scenario");
  const char *scenario = argv[1];
  if (strcmp(scenario, "window-failure") == 0 || strcmp(scenario, "layer-failure") == 0
      || strcmp(scenario, "font-null") == 0 || strcmp(scenario, "font-fallback") == 0
      || strcmp(scenario, "atlas-failure") == 0) {
    startup_failure(scenario);
  } else if (strcmp(scenario, "partial-cleanup") == 0) {
    prv_deinit();
    check(f.null_calls == 0, "cleanup before initialization is harmless");
    check(f.window_destroys == 0 && f.layer_destroys == 0 && f.font_unloads == 0,
          "empty cleanup does not free unowned resources");
  } else if (strcmp(scenario, "main") == 0) {
    watchface_main();
    check(f.event_loops == 1 && f.service_starts > 0,
          "successful main starts services and enters the event loop");
    check(f.fallback_title == 0 && f.fallback_restart == 0,
          "successful main does not show the error fallback");
    assert_clean();
    prv_deinit();
    assert_clean();
  } else {
    DialSecondsMode mode = DialSecondsAlways;
    if (strcmp(scenario, "off") == 0) mode = DialSecondsOff;
    if (strcmp(scenario, "shake") == 0 || strcmp(scenario, "timer-failure") == 0
        || strcmp(scenario, "cleanup") == 0) mode = DialSecondsOnShake;
    f.fail_message = strcmp(scenario, "message-failure") == 0;
    f.fail_health_subscription = strcmp(scenario, "health-subscription-failure") == 0;
    start(mode);
    if (strcmp(scenario, "always") == 0 || strcmp(scenario, "off") == 0) {
      int dirty = f.dirty;
      struct tm now = { 0 };
      f.tick(&now, f.tick_units);
      check(f.dirty == dirty + 1, "normal tick redraws the dial");
      f.battery((BatteryChargeState){ .charge_percent = 20 });
      check(f.dirty == dirty + 2, "battery event redraws the dial");
      prv_tap(ACCEL_AXIS_Z, 1);
      check(f.timer_count == 0, "Always/Off ignore tap gestures");
      app_event_loop();
    } else if (strcmp(scenario, "shake") == 0) {
      f.tap(ACCEL_AXIS_Z, 1);
      check(f.tick_units == SECOND_UNIT && f.timer_count == 1,
            "tap starts seconds and a timeout");
      f.tap(ACCEL_AXIS_Z, -1);
      check(f.timer_cancels == 1 && f.timer_count == 2 && !f.timers[0].live,
            "retrigger cancels the previous timeout");
      fire_timer(&f.timers[1]);
      check(f.tick_units == MINUTE_UNIT && !s_glance_active && !s_seconds_timer,
            "timeout hides seconds and restores minute ticks");
      f.tap(ACCEL_AXIS_Z, 1);
      f.focus(false);
      check(f.timer_cancels == 2 && f.tick_units == MINUTE_UNIT,
            "focus loss cancels active shake and restores minute ticks");
      int timers = f.timer_count;
      f.tap(ACCEL_AXIS_Z, 1);
      check(f.timer_count == timers, "background taps cannot start seconds");
      f.focus(true);
      check(f.tick_units == MINUTE_UNIT, "shake stays idle after focus returns");
      f.tap(ACCEL_AXIS_Z, 1);
      check(f.tick_units == SECOND_UNIT, "shake works again after focus returns");
    } else if (strcmp(scenario, "timer-failure") == 0) {
      f.fail_timer = true;
      int errors = f.errors;
      f.tap(ACCEL_AXIS_X, 1);
      check(f.errors > errors && f.tick_units == MINUTE_UNIT
            && !s_seconds_timer && !s_glance_active, "timer failure logs and stays on minute ticks");
      f.fail_timer = false;
      f.tap(ACCEL_AXIS_X, 1);
      check(f.tick_units == SECOND_UNIT, "tap recovers after timer allocation failure");
    } else if (strcmp(scenario, "focus") == 0) {
      f.focus(false);
      check(f.tick_units == MINUTE_UNIT, "Always pauses seconds without focus");
      int dirty = f.dirty;
      struct tm now = { 0 };
      f.tick(&now, MINUTE_UNIT);
      f.battery((BatteryChargeState){ .charge_percent = 70 });
      check(f.dirty == dirty, "background ticks and battery events do not redraw");
      f.focus(true);
      check(f.tick_units == SECOND_UNIT && f.dirty > dirty,
            "Always restores seconds and redraws on focus regain");
    } else if (strcmp(scenario, "cleanup") == 0) {
      f.tap(ACCEL_AXIS_Y, 1);
      check(f.timers[0].live, "cleanup scenario starts with an active timer");
    } else if (strcmp(scenario, "message-failure") == 0) {
      check(f.errors > 0 && f.tick_live, "messaging allocation failure is logged without losing time");
    } else if (strcmp(scenario, "health") == 0) {
#ifdef PBL_HEALTH
      check(!s_steps_available, "unavailable Health is not a zero-step reading");
      f.health_available = true; f.steps = 0;
      f.health(HealthEventSignificantUpdate, f.health_context);
      check(s_steps_available && s_steps == 0, "available zero-step data is distinct");
      int dirty = f.dirty;
      f.steps = 1234;
      f.health(HealthEventMovementUpdate, f.health_context);
      check(s_steps == 1234 && f.dirty == dirty + 1, "movement refreshes steps and redraws");
      dirty = f.dirty;
      f.health(HealthEventMovementUpdate, f.health_context);
      check(f.dirty == dirty, "unchanged Health data does not redraw");
      int reads = f.health_reads;
      struct tm now = { 0 };
      f.tick(&now, SECOND_UNIT);
      check(f.health_reads == reads, "second ticks do not poll Health");
      f.steps = 0;
      f.tick(&now, MINUTE_UNIT);
      check(s_steps == 0 && f.health_reads == reads + 1, "minute tick handles midnight reset");
      f.focus(false);
      dirty = f.dirty;
      f.steps = 42;
      f.health(HealthEventMovementUpdate, f.health_context);
      check(s_steps == 42 && f.dirty == dirty, "background Health updates do not redraw");
      f.health_available = false;
      f.focus(true);
      check(!s_steps_available, "focus regain refreshes Health availability");
#else
      check(!s_steps_available && !s_steps, "non-Health build reports unavailable steps");
#endif
    } else if (strcmp(scenario, "health-subscription-failure") == 0) {
#ifdef PBL_HEALTH
      check(!f.health_live && f.warnings > 0, "Health subscription failure is logged");
      f.health_available = true; f.steps = 100;
      struct tm now = { 0 };
      f.tick(&now, MINUTE_UNIT);
      check(s_steps_available && s_steps == 100, "minute polling survives Health subscription failure");
#endif
    } else {
      check(false, "unknown scenario");
    }
    prv_deinit();
    assert_clean();
  }
  puts("lifecycle test passed");
  return 0;
}
