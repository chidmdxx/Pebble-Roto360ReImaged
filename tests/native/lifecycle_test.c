#include "pebble.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static struct tm *fake_localtime(const time_t *timer);
#define localtime fake_localtime
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
#undef localtime

struct Layer { GRect bounds; LayerUpdateProc update; bool live; bool root; };
struct Window { Layer root; WindowHandlers handlers; bool live; bool pushed; };
struct AppTimer { AppTimerCallback callback; void *context; bool live; };
struct GBitmap { uint8_t data[80]; GColor *palette; };

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
  bool disable_steps_saved, fail_persist;
  int32_t disable_steps;
  bool center_saved;
  int32_t center;
  int weather_requests;
  int lines;
  int health_reads;
  bool capture, use_fake_time;
  struct tm now;
  int sectors, label_count;
  bool capture_center_lines, capture_music, antialiased;
  uint8_t stroke_width;
  int center_line_count, date_top, weekday_top;
  GRect music_rect;
  struct {
    GPoint start, end;
    uint8_t stroke_width;
    bool antialiased;
  } center_lines[5];
  struct {
    char text[4];
    GPoint center;
    int32_t angle;
    int sectors;
  } labels[25];
#ifdef PBL_HEALTH
  HealthEventHandler health;
  void *health_context;
  bool health_available;
  HealthValue steps;
#endif
} f;

static struct tm *fake_localtime(const time_t *timer) {
  return f.use_fake_time ? &f.now : localtime(timer);
}

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
  f.window.root = (Layer){ .bounds = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT),
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
  (void)bitmap; (void)height; (void)color;
  check(f.label_count < 25, "label recording capacity");
  int index = f.label_count++;
  snprintf(f.labels[index].text, sizeof(f.labels[index].text), "%s", text);
  f.labels[index].center = center;
  f.labels[index].angle = angle;
  f.labels[index].sectors = f.sectors;
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
bool persist_exists(int key) {
  return (key == SETTINGS_KEY_SECONDS && f.mode_saved)
      || (key == SETTINGS_KEY_DISABLE_STEPS && f.disable_steps_saved)
      || (key == SETTINGS_KEY_CENTER_DISPLAY && f.center_saved);
}
int persist_get_size(int key) { (void)key; return sizeof(int32_t); }
int persist_read_data(int key, void *buffer, size_t size) {
  (void)key; (void)buffer; (void)size; return -1;
}
int32_t persist_read_int(int key) {
  return key == SETTINGS_KEY_CENTER_DISPLAY ? f.center
       : key == SETTINGS_KEY_DISABLE_STEPS ? f.disable_steps : f.mode;
}
int persist_write_int(int key, int32_t value) {
  if (f.fail_persist) return -1;
  if (key == SETTINGS_KEY_DISABLE_STEPS) {
    f.disable_steps_saved = true;
    f.disable_steps = value;
  } else if (key == SETTINGS_KEY_CENTER_ORDER) {
    f.center_saved = true;
    f.center = value;
  }
  return sizeof(value);
}
Tuple *dict_find(const DictionaryIterator *iterator, uint32_t key) {
  if (key == MESSAGE_KEY_ShowSeconds) return iterator->seconds;
  if (key == MESSAGE_KEY_StepGoal) return iterator->goal;
  if (key == MESSAGE_KEY_DisableSteps) return iterator->disable_steps;
  if (key == MESSAGE_KEY_CenterDisplay) return iterator->center;
  if (key == MESSAGE_KEY_CenterOrder) return iterator->order;
  if (key == MESSAGE_KEY_MusicTitle) return iterator->music;
  if (key == MESSAGE_KEY_NotificationCount) return iterator->notifications;
  if (key == MESSAGE_KEY_MusicExpires) return iterator->music_expires;
  if (key == MESSAGE_KEY_NotificationsExpire) return iterator->notifications_expire;
  if (key == MESSAGE_KEY_SlotReset) return iterator->slot_reset;
  if (key == MESSAGE_KEY_SlotIndex) return iterator->slot_index;
  if (key == MESSAGE_KEY_SlotKind) return iterator->slot_kind;
  if (key == MESSAGE_KEY_SlotText) return iterator->slot_text;
  if (key == MESSAGE_KEY_SlotExpires) return iterator->slot_expires;
  if (key == MESSAGE_KEY_WeatherUnit) return iterator->weather_unit;
  if (key == MESSAGE_KEY_WeatherTemperature) return iterator->temperature;
  if (key == MESSAGE_KEY_WeatherPixels) return iterator->pixels;
  if (key == MESSAGE_KEY_WeatherPalette) return iterator->palette;
  return key == MESSAGE_KEY_WeatherExpires ? iterator->expires : NULL;
}
AppMessageInboxReceived app_message_register_inbox_received(AppMessageInboxReceived handler) {
  AppMessageInboxReceived old = f.inbox; f.inbox = handler; return old;
}
AppMessageInboxDropped app_message_register_inbox_dropped(AppMessageInboxDropped handler) {
  AppMessageInboxDropped old = f.dropped; f.dropped = handler; return old;
}
AppMessageOutboxFailed app_message_register_outbox_failed(AppMessageOutboxFailed handler) {
  (void)handler; return NULL;
}
AppMessageResult app_message_outbox_begin(DictionaryIterator **iterator) {
  static DictionaryIterator outbox;
  *iterator = &outbox;
  return APP_MSG_OK;
}
DictionaryResult dict_write_uint8(DictionaryIterator *iterator, uint32_t key, uint8_t value) {
  (void)iterator; (void)key; (void)value; return DICT_OK;
}
AppMessageResult app_message_outbox_send(void) { f.weather_requests++; return APP_MSG_OK; }
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
void graphics_context_set_stroke_width(GContext *context, uint8_t width) { (void)context; f.stroke_width = width; }
void graphics_context_set_antialiased(GContext *context, bool enabled) { (void)context; f.antialiased = enabled; }
void graphics_context_set_compositing_mode(GContext *context, GCompOp op) {
  (void)context; (void)op;
}
void graphics_draw_bitmap_in_rect(GContext *context, const GBitmap *bitmap, GRect rect) {
  (void)context; (void)rect; valid(bitmap);
}
GSize graphics_text_layout_get_content_size(const char *text, GFont font, GRect rect,
                                            GTextOverflowMode overflow, GTextAlignment alignment) {
  (void)font; (void)rect; (void)overflow; (void)alignment;
  return GSize((int16_t)(strlen(text) * 5), 10);
}
void graphics_fill_rect(GContext *context, GRect rect, uint16_t radius, GCornerMask corners) {
  (void)context; (void)rect; (void)radius; (void)corners;
}
void graphics_fill_circle(GContext *context, GPoint point, uint16_t radius) {
  (void)context; (void)point; (void)radius;
}
void graphics_fill_radial(GContext *context, GRect rect, GOvalScaleMode mode,
                          uint16_t inset, int32_t start, int32_t end) {
  (void)context; (void)rect; (void)mode; (void)inset; (void)start; (void)end;
  f.sectors++;
}
void graphics_draw_line(GContext *context, GPoint start, GPoint end) {
  (void)context;
  if (f.capture_center_lines) {
    check(f.center_line_count < 5, "notification bell has no extra lines");
    int i = f.center_line_count++;
    f.center_lines[i].start = start;
    f.center_lines[i].end = end;
    f.center_lines[i].stroke_width = f.stroke_width;
    f.center_lines[i].antialiased = f.antialiased;
  }
  f.lines++;
}
void graphics_draw_text(GContext *context, const char *text, GFont font, GRect rect,
                        GTextOverflowMode overflow, GTextAlignment alignment,
                        GTextAttributes *attributes) {
  (void)context; (void)overflow; (void)alignment; (void)attributes;
  valid(font);
  check(rect.size.w > 0 && rect.size.h > 0, "draw text into a visible rectangle");
  if ((f.capture_center_lines || f.capture_music) && strcmp(text, "NOV 08") == 0) {
    f.date_top = rect.origin.y + CENTER_FONT_SIZE - CENTER_CAP_HEIGHT;
  }
  if (f.capture_music && strcmp(text, "SUNDAY") == 0) {
    f.weekday_top = rect.origin.y + CENTER_FONT_SIZE - CENTER_CAP_HEIGHT;
  }
  if (f.capture_music && strcmp(text, "Agyp") == 0) {
    f.music_rect = rect;
    check(font == &f.system_font && overflow == GTextOverflowModeTrailingEllipsis,
          "music keeps the system font and ellipsis");
  }
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
void gpath_draw_filled(GContext *context, GPath *path) {
  (void)context; (void)path; f.sectors++;
}
void gpath_destroy(GPath *path) { (void)path; }
GBitmap *graphics_capture_frame_buffer(GContext *context) {
  return f.capture ? (GBitmap *)context : NULL;
}
void graphics_release_frame_buffer(GContext *context, GBitmap *bitmap) {
  (void)context; valid(bitmap);
}
GBitmapFormat gbitmap_get_format(const GBitmap *bitmap) { valid(bitmap); return GBitmapFormat8Bit; }
GBitmap *gbitmap_create_blank_with_palette(GSize size, GBitmapFormat format, GColor *palette,
                                           bool free_palette) {
  (void)size; (void)format;
  check(free_palette, "weather bitmap owns its palette");
  GBitmap *bitmap = calloc(1, sizeof(*bitmap));
  if (bitmap) bitmap->palette = palette;
  return bitmap;
}
void gbitmap_destroy(GBitmap *bitmap) { free(bitmap->palette); free(bitmap); }
uint8_t *gbitmap_get_data(const GBitmap *bitmap) { return (uint8_t *)bitmap->data; }
uint16_t gbitmap_get_bytes_per_row(const GBitmap *bitmap) { (void)bitmap; return 8; }

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
  check(f.focus && f.tick && f.inbox && f.dropped,
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
  } else if (strcmp(scenario, "music-layout") == 0) {
    start(DialSecondsOff);
    strcpy(s_music_title, "Agyp");
    s_center_slots[0].kind = 1;
    strcpy(s_center_slots[0].text, "Agyp");
    struct tm now = { .tm_wday = 0, .tm_mon = 10, .tm_mday = 8 };
    GContext context = { 0 };
    f.capture_music = true;
    for (int plugin = 0; plugin < 2; plugin++) {
      s_center_order = (plugin ? CenterPlugin : CenterMusic) + 1;
      f.date_top = f.weekday_top = -1;
      f.music_rect = GRect(0, 0, 0, 0);
      prv_draw_center(&context, f.layer.bounds, &now);
      int center = f.layer.bounds.size.h / 2;
      check(f.music_rect.size.w > 0 && f.date_top >= 0 && f.weekday_top >= 0,
            "draw title, weekday and date");
      check(f.music_rect.size.h >= 16, "music box fits the system font");
      check(f.music_rect.origin.y + 10 == center, "title glyph bounds center on the dial");
      check(f.weekday_top == center + dial_center_line_top(CENTER_CAP_HEIGHT, CENTER_GAP, 0),
            "title does not move the weekday");
      check(f.date_top == center + dial_center_line_top(CENTER_CAP_HEIGHT, CENTER_GAP, 2)
            + (CENTER_CAP_HEIGHT < 12 ? 2 : 0), "title preserves the date position");
      check(f.music_rect.origin.y + 15 < f.date_top, "title descenders stay above the date");
    }
    f.capture_music = false;
    prv_deinit();
    assert_clean();
  } else if (strcmp(scenario, "notification-layout") == 0) {
    start(DialSecondsOff);
    s_center_order = CenterNotifications + 1;
    struct tm now = { .tm_wday = 0, .tm_mon = 10, .tm_mday = 8 };
    GContext context = { 0 };
    int middle_top = f.layer.bounds.size.h / 2
                     + dial_center_line_top(CENTER_CAP_HEIGHT, CENTER_GAP, 1);
    for (int count = 1; count <= 999; count = count * 10 + 9) {
      s_notification_count = count;
      f.capture_center_lines = true;
      f.center_line_count = 0;
      f.date_top = -1;
      graphics_context_set_stroke_width(&context, 3);
      graphics_context_set_antialiased(&context, true);
      prv_draw_center(&context, f.layer.bounds, &now);
      check(f.center_line_count == 5 && f.date_top >= 0, "draw bell and date");
      for (int i = 0; i < f.center_line_count; i++) {
        GPoint start = f.center_lines[i].start, end = f.center_lines[i].end;
        check(f.center_lines[i].stroke_width == 1 && !f.center_lines[i].antialiased,
              "bell stroke stays within its pixel geometry");
        check(start.y >= middle_top && end.y >= middle_top
              && start.y < middle_top + CENTER_CAP_HEIGHT
              && end.y < middle_top + CENTER_CAP_HEIGHT,
              "bell stays within the middle cap-height bounds");
        check(start.y < f.date_top - CENTER_GAP && end.y < f.date_top - CENTER_GAP,
              "bell leaves a clear gap above the date");
        check(start.x >= 0 && end.x >= 0 && start.x < f.layer.bounds.size.w
              && end.x < f.layer.bounds.size.w, "bell stays within screen bounds");
      }
      check(f.antialiased, "restore antialiasing after the bell");
    }
    f.capture_center_lines = false;
    prv_deinit();
    assert_clean();
  } else if (strcmp(scenario, "minute-layout") == 0) {
    start(DialSecondsOff);
    f.capture = true;
    f.use_fake_time = true;
    f.now = (struct tm){ .tm_hour = 23, .tm_wday = 3, .tm_mon = 10, .tm_mday = 8 };
    int half_w = f.layer.bounds.size.w / 2;
    int half_h = f.layer.bounds.size.h / 2;
    int unit = half_w < half_h ? half_w : half_h;
    Dial dial = {
      .center = grect_center_point(&f.layer.bounds),
      .center_q4 = GPoint(f.layer.bounds.size.w * 8, f.layer.bounds.size.h * 8),
      .outer = PBL_IF_ROUND_ELSE(((DialTrack){ unit, unit, unit }),
          ((DialTrack){ half_w, half_h, prv_scale(unit, SQUARE_CORNER) })),
      .unit = unit,
    };
    DialTrack minute_text = prv_track(&dial, MINUTE_TEXT_RADIUS);
    int32_t normal = 0;
    GPoint pointer = prv_track_walk_q4(&dial, minute_text, 0, &normal);
    for (int minute = 0; minute <= 60; minute++) {
      f.now.tm_min = minute % 60;
      f.label_count = 0;
      f.sectors = 0;
      GContext context = { 0 };
      prv_dial_update(&f.layer, &context);
      check(f.label_count == 25, "draw all ring labels plus hour and minute readouts");
      for (int slot = 0; slot < DIAL_MINUTE_SLOTS; slot++) {
        char expected[4];
        snprintf(expected, sizeof(expected), "%02d", slot * 5);
        check(strcmp(f.labels[slot].text, expected) == 0, "ring label value");
        check(f.labels[slot].sectors == 0, "draw ring labels beneath pointer sectors");
        int32_t expected_normal = 0;
        GPoint expected_center = prv_track_walk_q4(&dial, minute_text,
            dial_minute_angle(slot * 5, minute % 60), &expected_normal);
        check(f.labels[slot].center.x == expected_center.x
              && f.labels[slot].center.y == expected_center.y
              && f.labels[slot].angle == expected_normal,
              "ring labels must rotate uniformly without pointer avoidance or hiding");
      }
      check(f.labels[24].sectors == 3, "draw exact minute above all pointer sectors");
      check(f.labels[24].center.x == pointer.x && f.labels[24].center.y == pointer.y
            && f.labels[24].angle == normal, "minute window must remain fixed at the top");
      char expected[4];
      snprintf(expected, sizeof(expected), "%02d", minute % 60);
      check(strcmp(f.labels[24].text, expected) == 0, "exact minute readout value");
      if (minute % 5 == 0) {
        int slot = minute % 60 / 5;
        check(f.labels[slot].center.x == pointer.x && f.labels[slot].center.y == pointer.y,
              "ring label passes directly beneath the fixed minute window");
      }
    }
    prv_deinit();
    assert_clean();
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
    if (strcmp(scenario, "disable-steps-saved") == 0
        || strcmp(scenario, "disable-steps-invalid-saved") == 0) {
      f.disable_steps_saved = true;
      f.disable_steps = strcmp(scenario, "disable-steps-saved") == 0 ? 1 : 2;
    }
    if (strcmp(scenario, "weather") == 0 || strcmp(scenario, "center-saved") == 0) {
      f.center_saved = true;
      f.center = CenterWeather;
    }
    start(mode);
    if (strcmp(scenario, "weather") == 0) {
      check(f.weather_requests == 1 && !f.battery_live, "saved weather requests a reading without battery subscription");
      TupleValue unit_value = { .int32 = 1 }, temperature_value = { .int32 = -3 };
      union { TupleValue align; uint8_t bytes[50]; } pixel_data = { 0 };
      union { TupleValue align; uint8_t bytes[16]; } palette_data = { 0 };
      Tuple unit = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &unit_value };
      Tuple temperature = { .type = TUPLE_INT, .length = sizeof(int32_t),
                            .value = &temperature_value };
      Tuple pixels = { .type = TUPLE_BYTE_ARRAY, .length = 50, .value = &pixel_data.align };
      Tuple palette = { .type = TUPLE_BYTE_ARRAY, .length = 16, .value = &palette_data.align };
      DictionaryIterator message = { .weather_unit = &unit, .temperature = &temperature,
                                     .pixels = &pixels, .palette = &palette };
      int dirty = f.dirty;
      f.inbox(&message, NULL);
      check(s_weather_unit == 1 && s_weather_temperature == -3 && s_weather_icon,
            "weather update displays temperature and icon");
      check(f.dirty == dirty + 1, "weather update redraws");
      app_event_loop();
      pixels.length = 49;
      f.inbox(&message, NULL);
      check(!s_weather_icon && f.errors > 0, "malformed icon is rejected");
      temperature_value.int32 = 1000;
      f.inbox(&message, NULL);
      check(s_weather_temperature == -3, "invalid temperatures do not overwrite the reading");
      unit_value.int32 = 0;
      f.inbox(&message, NULL);
      check(s_weather_unit == 0 && !s_weather_icon, "unavailable weather clears the display");
      unit_value.int32 = 1;
      temperature_value.int32 = 10;
      TupleValue expiry_value = { .int32 = (int32_t)time(NULL) + 60 };
      Tuple expires = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &expiry_value };
      message.expires = &expires;
      message.pixels = NULL;
      message.palette = NULL;
      f.inbox(&message, NULL);
      check(s_weather_unit == 1, "unexpired reading is displayed");
      s_weather_expires = (int32_t)time(NULL) - 1;
      struct tm now = { 0 };
      f.tick(&now, MINUTE_UNIT);
      check(s_weather_unit == 0, "expired reading is cleared on minute tick");
      expiry_value.int32 = (int32_t)time(NULL) - 1;
      f.inbox(&message, NULL);
      check(s_weather_unit == 0, "already expired reading is not displayed");
    } else if (strcmp(scenario, "center-saved") == 0) {
      check(prv_center_enabled(CenterWeather) && f.weather_requests == 1 && !f.battery_live,
            "saved weather selection is restored independently of step settings");
    } else if (strcmp(scenario, "center") == 0) {
      check(prv_center_selected() == CenterBattery && f.battery_live && f.weather_requests == 0,
            "battery is default and does not request weather");
      TupleValue value = { .int32 = CenterNone };
      Tuple center = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &value };
      DictionaryIterator iter = { .center = &center };
      f.inbox(&iter, NULL);
      check(prv_center_selected() == CenterNone && !f.battery_live && f.center_saved && f.center == CenterNone + 1,
            "none persists and stops battery updates");
      value.int32 = CenterWeather;
      f.inbox(&iter, NULL);
      check(prv_center_enabled(CenterWeather) && !f.battery_live && f.center == CenterWeather + 1,
            "weather selection persists");
      TupleValue unit_value = { .int32 = 1 }, temperature_value = { .int32 = 18 };
      Tuple unit = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &unit_value };
      Tuple temperature = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &temperature_value };
      DictionaryIterator weather = { .weather_unit = &unit, .temperature = &temperature };
      f.inbox(&weather, NULL);
      check(s_weather_unit == 1, "selected weather accepts readings");
      value.int32 = CenterBattery;
      f.inbox(&iter, NULL);
      check(prv_center_selected() == CenterBattery && f.battery_live && !s_weather_unit,
            "battery selection clears weather and resumes battery updates");
      f.inbox(&weather, NULL);
      check(!s_weather_unit, "late weather cannot restore a reading after opting out");
      int dirty = f.dirty;
      f.battery(battery_state_service_peek());
      check(f.dirty == dirty + 1, "battery updates redraw battery mode");
      value.int32 = 3;
      int errors = f.errors;
      f.inbox(&iter, NULL);
      check(f.errors > errors && prv_center_selected() == CenterBattery && f.center == CenterBattery + 1,
            "invalid center selection is rejected");
      TupleValue order_value = { .int32 = 4 | (5 << 3) | (3 << 6) | (1 << 9) };
      Tuple order = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &order_value };
      DictionaryIterator priority = { .order = &order };
      f.inbox(&priority, NULL);
      check(prv_center_selected() == CenterBattery && f.battery_live, "empty sources fall through to battery");
      f.inbox(&weather, NULL);
      check(prv_center_selected() == CenterWeather, "weather beats battery");
      TupleValue count_value = { .int32 = 2 };
      Tuple count = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &count_value };
      DictionaryIterator count_message = { .notifications = &count };
      f.inbox(&count_message, NULL);
      check(prv_center_selected() == CenterNotifications, "notifications beat weather");
      union { TupleValue align; char text[8]; } title = { .text = "Track" };
      Tuple music = { .type = TUPLE_CSTRING, .length = 6, .value = &title.align };
      DictionaryIterator music_message = { .music = &music };
      f.inbox(&music_message, NULL);
      check(prv_center_selected() == CenterMusic, "music takes first priority");
      title.text[0] = '\0'; music.length = 1;
      f.inbox(&music_message, NULL);
      check(prv_center_selected() == CenterNotifications, "paused music falls through");
      count_value.int32 = 0;
      f.inbox(&count_message, NULL);
      check(prv_center_selected() == CenterWeather, "zero notifications fall through");
      strcpy(title.text, "Track"); music.length = 6;
      TupleValue expiry_value = { .int32 = (int32_t)time(NULL) + 60 };
      Tuple expires = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &expiry_value };
      music_message.music_expires = &expires;
      f.inbox(&music_message, NULL);
      check(prv_center_selected() == CenterMusic, "unexpired music is usable");
      s_music_expires = (int32_t)time(NULL) - 1;
      struct tm now = { 0 };
      f.tick(&now, MINUTE_UNIT);
      check(prv_center_selected() == CenterWeather, "expired music falls through on a minute tick");
      order_value.int32 = 4 | (4 << 3);
      errors = f.errors;
      f.inbox(&priority, NULL);
      check(f.errors > errors && prv_center_selected() == CenterWeather, "duplicate priority is rejected");
      order_value.int32 = 6 | (1 << 3);
      f.inbox(&priority, NULL);
      check(prv_center_selected() == CenterBattery, "empty shared order falls back to battery");
      TupleValue index_value = { .int32 = 1 }, kind_value = { .int32 = 1 };
      TupleValue slot_expiry_value = { .int32 = (int32_t)time(NULL) + 60 };
      union { TupleValue align; char text[8]; } slot_title = { .text = "Track" };
      Tuple index = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &index_value };
      Tuple kind = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &kind_value };
      Tuple slot_text = { .type = TUPLE_CSTRING, .length = 6, .value = &slot_title.align };
      Tuple slot_expires = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &slot_expiry_value };
      DictionaryIterator slot = { .slot_index = &index, .slot_kind = &kind,
        .slot_text = &slot_text, .slot_expires = &slot_expires };
      f.inbox(&slot, NULL);
      check(prv_center_selected() == CenterPlugin && !strcmp(s_center_slots[1].text, "Track"),
            "shared complication is shown");
      index_value.int32 = 0;
      strcpy(slot_title.text, "First"); slot_text.length = 6;
      f.inbox(&slot, NULL);
      check(!strcmp(s_center_slots[0].text, "First"), "higher-priority slot is retained");
      s_center_slots[0].expires = (int32_t)time(NULL) - 1;
      check(prv_center_selected() == CenterPlugin && s_center_slots[1].kind,
            "expired priority falls through to another cached slot");
      s_center_slots[1].expires = (int32_t)time(NULL) - 1;
      check(prv_center_selected() == CenterBattery, "expired shared slots fall back to battery");
      order_value.int32 = 1;
      f.inbox(&priority, NULL);
      check(!s_center_slots[0].kind && !s_center_slots[1].kind, "opting out clears shared readings");
      f.inbox(&slot, NULL);
      check(!s_center_slots[0].kind, "late shared reading is ignored after opting out");
    } else if (strcmp(scenario, "always") == 0 || strcmp(scenario, "off") == 0) {
      int dirty = f.dirty;
      struct tm now = { 0 };
      f.tick(&now, f.tick_units);
      check(f.dirty == dirty + 1, "normal tick redraws the dial");
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
      check(f.dirty == dirty, "background ticks do not redraw");
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
    } else if (strncmp(scenario, "disable-steps", 13) == 0) {
      TupleValue value = { .int32 = 1 };
      Tuple disabled = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &value };
      DictionaryIterator iter = { .disable_steps = &disabled };
      if (strcmp(scenario, "disable-steps-invalid-saved") == 0) {
        check(!s_disable_steps && f.errors > 0, "invalid persisted bool logs and preserves enabled steps");
      } else if (strcmp(scenario, "disable-steps-invalid") == 0) {
        TupleValue seconds_value = { .int32 = DialSecondsOff };
        Tuple seconds = { .type = TUPLE_INT, .length = sizeof(int32_t), .value = &seconds_value };
        iter.seconds = &seconds;
        for (int i = 0; i < 4; i++) {
          disabled.type = i == 2 ? TUPLE_CSTRING : TUPLE_INT;
          disabled.length = i == 3 ? 1 : sizeof(int32_t);
          value.int32 = i == 0 ? -1 : i == 1 ? 2 : 1;
          int errors = f.errors;
          f.inbox(&iter, NULL);
          check(f.errors == errors + 1 && !s_disable_steps && !f.disable_steps_saved,
                "invalid bool is logged without applying or persisting it");
          check(s_seconds_mode == DialSecondsAlways, "invalid bool rejects the whole settings message");
        }
      } else {
        if (strcmp(scenario, "disable-steps-saved") == 0) {
          check(s_disable_steps && !f.health_live && f.health_reads == 0,
                "saved disabled counter never starts Health or reads steps");
        } else {
          int errors = f.errors;
          f.fail_persist = strcmp(scenario, "disable-steps-save-failure") == 0;
          f.inbox(&iter, NULL);
          check(s_disable_steps, "settings disable the step counter immediately");
          if (f.fail_persist) {
            check(f.errors > errors && !f.disable_steps_saved, "failed persistence is reported");
          } else {
            check(f.disable_steps_saved && f.disable_steps == 1, "disabled bool is persisted");
          }
          f.fail_persist = false;
        }
        check(!f.health_live && !s_steps_available && s_steps == 0,
              "disabled counter unsubscribes and clears stale step data");
        int reads = f.health_reads;
        int dirty = f.dirty;
        check(!prv_read_steps(), "disabled step reads are inert");
        struct tm now = { 0 };
        f.tick(&now, MINUTE_UNIT);
        f.focus(false);
        f.focus(true);
#ifdef PBL_HEALTH
        prv_health(HealthEventMovementUpdate, NULL);
#endif
        check(f.health_reads == reads, "disabled ticks, focus and late Health events never read Health");
        check(f.tick_units == SECOND_UNIT && f.dirty > dirty,
              "disabling steps preserves seconds and time redraws");
        GContext context = { 0 };
        Dial dial = { .outer = { .half_w = 60, .half_h = 60, .corner = 60 }, .unit = 60 };
        int lines = f.lines;
        prv_draw_step_progress(&context, &dial);
        check(f.lines == lines, "disabled counter draws neither progress nor unavailable dash");
#ifdef PBL_HEALTH
        f.health_available = true;
        f.steps = 4321;
#endif
        f.fail_health_subscription = strcmp(scenario, "disable-steps-subscription-failure") == 0;
        value.int32 = 0;
        f.inbox(&iter, NULL);
        check(!s_disable_steps && f.disable_steps_saved && f.disable_steps == 0,
              "enabling the counter persists the bool");
#ifdef PBL_HEALTH
        check(s_steps_available && s_steps == 4321 && f.health_reads == reads + 1,
              "enabling immediately refreshes today's total");
        check(f.health_live == !f.fail_health_subscription,
              "enabling subscribes to Health when available");
        if (f.fail_health_subscription) {
          check(f.warnings > 0, "failed re-subscription is logged");
          f.steps = 5000;
          f.tick(&now, MINUTE_UNIT);
          check(s_steps == 5000, "minute polling survives re-subscription failure");
        }
#else
        check(!s_steps_available && f.health_reads == 0, "toggle is safe without Health support");
#endif
        lines = f.lines;
        prv_draw_step_progress(&context, &dial);
        check(f.lines > lines, "enabling restores step progress or unavailable dash");
      }
    } else {
      check(false, "unknown scenario");
    }
    prv_deinit();
    assert_clean();
  }
  puts("lifecycle test passed");
  return 0;
}
