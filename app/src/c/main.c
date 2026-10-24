#include <pebble.h>

#include "dial.h"
#include "numerals.h"

#if PBL_DISPLAY_WIDTH >= 260
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_20
#define CENTER_FONT_SIZE 20
#define CENTER_CAP_HEIGHT 14
#elif PBL_DISPLAY_WIDTH >= 200
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_18
#define CENTER_FONT_SIZE 18
#define CENTER_CAP_HEIGHT 13
#elif defined(PBL_ROUND)
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_14
#define CENTER_FONT_SIZE 14
#define CENTER_CAP_HEIGHT 10
#else
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_12
#define CENTER_FONT_SIZE 12
#define CENTER_CAP_HEIGHT 9
#endif
#define CENTER_GAP 2
#define SETTINGS_KEY_SECONDS 1
#define SETTINGS_KEY_STEP_GOAL 2
#define SECONDS_GLANCE_MS 10000

// Ring proportions, as fractions of the dial radius in 1/1000ths.
#define RIM_INNER 945
#define HOUR_INNER 630
#define MINUTE_INNER 400
#define HOUR_TEXT_RADIUS 787
#define HOUR_TEXT_HEIGHT 222
#define MINUTE_TEXT_RADIUS 515
#define MINUTE_TEXT_HEIGHT 130
#define SHIELD_HALF_ANGLE 15
#define MINUTE_BOX_HALF_ANGLE 10

// Corner radius of the outer track on rectangular screens, per mille of the
// shorter half-side.
#define SQUARE_CORNER 420

#define COLOR_RIM GColorDarkGray
#define COLOR_HOUR_BAND GColorBlack
#define COLOR_MINUTE_BAND GColorDarkGray
#define COLOR_CENTER GColorBlack
#define COLOR_SHIELD_EDGE GColorCobaltBlue
#define COLOR_SHIELD_HOUR GColorOxfordBlue
#define COLOR_SHIELD_MINUTE GColorCobaltBlue

static Window *s_window;
static Layer *s_dial_layer;
static GFont s_center_font;
static bool s_numerals_ready;
static bool s_init_failed;
static bool s_services_started;
static DialSecondsMode s_seconds_mode = DialSecondsAlways;
static AppTimer *s_seconds_timer;
static bool s_glance_active;
static bool s_in_focus = true;
static bool s_second_ticks;
static int32_t s_step_goal = DIAL_STEP_GOAL_DEFAULT;
static int32_t s_steps;
static bool s_steps_available;
static bool s_steps_initialized;
#if defined(PBL_HEALTH)
static bool s_health_subscribed;
#endif

typedef struct {
  GPoint center;
  GPoint center_q4;
  DialTrack outer;
  int unit;
} Dial;

static int prv_scale(int unit, int permille) {
  return unit * permille / 1000;
}

// The track at `permille` of the dial size, measured like a radius. On
// rectangular screens inner tracks keep a little rounding at the corners.
static DialTrack prv_track(const Dial *dial, int permille) {
  DialTrack t = dial_track_inset(dial->outer, dial->unit - prv_scale(dial->unit, permille));
  int min_corner = prv_scale(dial->unit, 120);
  if (t.corner < min_corner) {
    t.corner = min_corner;
  }
  return t;
}

static bool prv_is_circle(DialTrack t) {
  return t.half_w == t.half_h && t.corner >= t.half_w;
}

static void prv_fill_track(GContext *ctx, GPoint c, DialTrack t, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  if (prv_is_circle(t)) {
    graphics_fill_circle(ctx, c, t.half_w);
    return;
  }
  int k = t.corner;
  graphics_fill_rect(ctx, GRect(c.x - t.half_w, c.y - t.half_h + k, t.half_w * 2,
                                (t.half_h - k) * 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x - t.half_w + k, c.y - t.half_h, (t.half_w - k) * 2,
                                t.half_h * 2), 0, GCornerNone);
  if (k > 0) {
    int ix = t.half_w - k;
    int iy = t.half_h - k;
    graphics_fill_circle(ctx, GPoint(c.x - ix, c.y - iy), k);
    graphics_fill_circle(ctx, GPoint(c.x + ix - 1, c.y - iy), k);
    graphics_fill_circle(ctx, GPoint(c.x - ix, c.y + iy - 1), k);
    graphics_fill_circle(ctx, GPoint(c.x + ix - 1, c.y + iy - 1), k);
  }
}

#if PBL_DISPLAY_WIDTH >= 200
// Perimeter of `track` in 1/16 pixel units, matching prv_track_walk_q4.
static int32_t prv_track_length_q4(DialTrack track) {
  int rc = track.corner;
  if (rc > track.half_w) {
    rc = track.half_w;
  }
  if (rc > track.half_h) {
    rc = track.half_h;
  }
  int32_t arc = rc * 16 * 25736 / 16384;
  return 4 * ((track.half_w - rc) * 16 + (track.half_h - rc) * 16 + arc);
}
#endif

// Walks `turn` (0 to TRIG_MAX_ANGLE, clockwise from 12 o'clock) of the way
// around `track` at constant speed. Returns the point in 1/16 pixel units and
// sets `normal` to the outward direction there, which is how labels are
// rotated. On a circle this is the same as placing labels by angle; on a
// rounded rectangle it spaces them evenly along the edge.
static GPoint prv_track_walk_q4(const Dial *dial, DialTrack track, int32_t turn,
                                int32_t *normal) {
  int rc = track.corner;
  if (rc > track.half_w) {
    rc = track.half_w;
  }
  if (rc > track.half_h) {
    rc = track.half_h;
  }
  int32_t sx = (track.half_w - rc) * 16;
  int32_t sy = (track.half_h - rc) * 16;
  int32_t a = track.half_w * 16;
  int32_t b = track.half_h * 16;
  int32_t arc = rc * 16 * 25736 / 16384;  // rc * pi / 2
  int32_t total = 4 * (sx + sy + arc);
  int32_t p = (int32_t)((int64_t)(turn & (TRIG_MAX_ANGLE - 1)) * total / TRIG_MAX_ANGLE);

  // Segments clockwise from the top centre: straight edges alternate with arcs.
  const int32_t lengths[9] = { sx, arc, 2 * sy, arc, 2 * sx, arc, 2 * sy, arc, sx };
  int seg = 0;
  while (seg < 8 && p >= lengths[seg]) {
    p -= lengths[seg];
    seg++;
  }
  int32_t x = 0;
  int32_t y = 0;
  int32_t quarter = TRIG_MAX_ANGLE / 4;
  if (seg % 2 == 1) {
    int corner = seg / 2;  // 0 top-right, 1 bottom-right, 2 bottom-left, 3 top-left
    int32_t phi = corner * quarter + (arc > 0 ? p * quarter / arc : 0);
    int32_t cx = (corner == 0 || corner == 1) ? sx : -sx;
    int32_t cy = (corner == 1 || corner == 2) ? sy : -sy;
    x = cx + (int32_t)((int64_t)sin_lookup(phi) * rc * 16 / TRIG_MAX_RATIO);
    y = cy - (int32_t)((int64_t)cos_lookup(phi) * rc * 16 / TRIG_MAX_RATIO);
    *normal = phi;
  } else {
    switch (seg) {
      case 0: x = p; y = -b; *normal = 0; break;
      case 2: x = a; y = -sy + p; *normal = quarter; break;
      case 4: x = sx - p; y = b; *normal = 2 * quarter; break;
      case 6: x = -a; y = sy - p; *normal = 3 * quarter; break;
      default: x = -sx + p; y = -b; *normal = 0; break;
    }
  }
  return GPoint(dial->center_q4.x + x, dial->center_q4.y + y);
}

// A point on `track` in the direction `angle`, in 1/16 pixel units.
static GPoint prv_track_point_q4(const Dial *dial, DialTrack track, int32_t angle) {
  int32_t s = sin_lookup(angle);
  int32_t c = cos_lookup(angle);
  int32_t dist = dial_track_distance_q4(track, s, c);
  return GPoint(dial->center_q4.x + (int32_t)((int64_t)s * dist / TRIG_MAX_RATIO),
                dial->center_q4.y - (int32_t)((int64_t)c * dist / TRIG_MAX_RATIO));
}

static GPoint prv_q4_to_px(GPoint p) {
  return GPoint((p.x + 8) >> 4, (p.y + 8) >> 4);
}

static void prv_draw_step_progress(GContext *ctx, const Dial *dial) {
  DialTrack track = dial_track_inset(dial->outer, 2);
  int32_t start = DEG_TO_TRIGANGLE(SHIELD_HALF_ANGLE);
  int32_t sweep = dial_step_progress(s_steps, s_step_goal);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_stroke_color(ctx, GColorGreen);
  if (!s_steps_available) {
    // An amber dash means unavailable Health data, not zero steps.
    start = DEG_TO_TRIGANGLE(178);
    sweep = DEG_TO_TRIGANGLE(4);
    graphics_context_set_stroke_color(ctx, GColorOrange);
  }
  if (sweep == 0) {
    return;
  }
  GPoint previous = prv_q4_to_px(prv_track_point_q4(dial, track, start));
  int32_t end = start + sweep;
  for (int32_t angle = start; angle < end;) {
    angle += DEG_TO_TRIGANGLE(2);
    if (angle > end) {
      angle = end;
    }
    GPoint next = prv_q4_to_px(prv_track_point_q4(dial, track, angle));
    graphics_draw_line(ctx, previous, next);
    previous = next;
  }
}

// Fills the wedge between two tracks, centred on 12 o'clock.
static void prv_fill_sector(GContext *ctx, const Dial *dial, DialTrack outer, DialTrack inner,
                            int half_deg, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  if (prv_is_circle(outer) && prv_is_circle(inner)) {
    int r = outer.half_w;
    graphics_fill_radial(ctx, GRect(dial->center.x - r, dial->center.y - r, r * 2, r * 2),
                         GOvalScaleModeFitCircle, r - inner.half_w,
                         DEG_TO_TRIGANGLE(-half_deg), DEG_TO_TRIGANGLE(half_deg));
    return;
  }
  int32_t a = DEG_TO_TRIGANGLE(half_deg);
  GPoint points[4] = {
    prv_q4_to_px(prv_track_point_q4(dial, outer, -a)),
    prv_q4_to_px(prv_track_point_q4(dial, outer, a)),
    prv_q4_to_px(prv_track_point_q4(dial, inner, a)),
    prv_q4_to_px(prv_track_point_q4(dial, inner, -a)),
  };
  GPathInfo info = { .num_points = 4, .points = points };
  GPath *path = gpath_create(&info);
  if (path) {
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
}

static GBitmap *prv_capture(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) {
    return NULL;
  }
  GBitmapFormat format = gbitmap_get_format(fb);
  if (format != GBitmapFormat8Bit && format != GBitmapFormat8BitCircular) {
    graphics_release_frame_buffer(ctx, fb);
    return NULL;
  }
  return fb;
}

static void prv_draw_label(GBitmap *fb, const Dial *dial, int value, DialTrack track,
                           int32_t turn, int height, GColor color) {
  char text[4];
  snprintf(text, sizeof(text), "%02d", value);
  int32_t normal = 0;
  GPoint at = prv_track_walk_q4(dial, track, turn, &normal);
  numerals_draw(fb, text, at, normal, height, color);
}

static void prv_draw_center(GContext *ctx, GRect bounds, const struct tm *now) {
  char battery[6];
  char date[8];
  BatteryChargeState charge = battery_state_service_peek();
  dial_format_battery(battery, sizeof(battery), charge.charge_percent, charge.is_charging);
  dial_format_date(date, sizeof(date), now->tm_mon, now->tm_mday);

  GColor battery_color = GColorWhite;
  if (charge.is_charging) {
    battery_color = GColorGreen;
  } else if (charge.charge_percent <= 20) {
    battery_color = GColorRed;
  }

  const char *lines[] = { dial_day_name(now->tm_wday), battery, date };
  GColor colors[] = { GColorWhite, battery_color, GColorWhite };
  GPoint center = grect_center_point(&bounds);
  int box_w = bounds.size.w;
  for (int i = 0; i < 3; i++) {
    // Bebas capitals occupy [font size - cap height, font size) in the SDK font.
    int y = center.y + dial_center_line_top(CENTER_CAP_HEIGHT, CENTER_GAP, i)
            - (CENTER_FONT_SIZE - CENTER_CAP_HEIGHT);
    graphics_context_set_text_color(ctx, colors[i]);
    graphics_draw_text(ctx, lines[i], s_center_font,
                       GRect(bounds.origin.x, y, box_w, CENTER_FONT_SIZE + 2),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

static void prv_dial_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int half_w = bounds.size.w / 2;
  int half_h = bounds.size.h / 2;
  int unit = half_w < half_h ? half_w : half_h;
  Dial dial = {
    .center = grect_center_point(&bounds),
    .center_q4 = GPoint(bounds.size.w * 8, bounds.size.h * 8),
    .outer = PBL_IF_ROUND_ELSE(((DialTrack){ unit, unit, unit }),
                               ((DialTrack){ half_w, half_h, prv_scale(unit, SQUARE_CORNER) })),
    .unit = unit,
  };

  time_t t = time(NULL);
  struct tm *now = localtime(&t);
  bool is_24h = clock_is_24h_style();

  DialTrack rim_in = prv_track(&dial, RIM_INNER);
  DialTrack hour_in = prv_track(&dial, HOUR_INNER);
  DialTrack minute_in = prv_track(&dial, MINUTE_INNER);
  DialTrack hour_text = prv_track(&dial, HOUR_TEXT_RADIUS);
  DialTrack minute_text = prv_track(&dial, MINUTE_TEXT_RADIUS);
  int hour_h = prv_scale(unit, HOUR_TEXT_HEIGHT);
  int minute_h = prv_scale(unit, MINUTE_TEXT_HEIGHT);
  DialTrack rim = PBL_IF_ROUND_ELSE(dial_track_inset(dial.outer, -1), dial.outer);

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  prv_fill_track(ctx, dial.center, rim, COLOR_RIM);
  prv_fill_track(ctx, dial.center, rim_in, COLOR_HOUR_BAND);
  prv_fill_track(ctx, dial.center, hour_in, COLOR_MINUTE_BAND);
  prv_fill_track(ctx, dial.center, minute_in, COLOR_CENTER);
  prv_draw_step_progress(ctx, &dial);

  GBitmap *fb = s_numerals_ready ? prv_capture(ctx) : NULL;
  if (fb) {
#if PBL_DISPLAY_WIDTH >= 200
    // Big screens have room to keep the labels next to the pointer, moved just
    // clear of the minute box instead of hidden.
    int32_t track_len = prv_track_length_q4(minute_text);
    GPoint box_edge = prv_track_point_q4(&dial, minute_text,
                                         DEG_TO_TRIGANGLE(MINUTE_BOX_HALF_ANGLE));
    int32_t box_half_q4 = box_edge.x - dial.center_q4.x;
#endif
    for (int slot = 0; slot < DIAL_MINUTE_SLOTS; slot++) {
      int label = dial_minute_label(slot);
#if PBL_DISPLAY_WIDTH >= 200
      if (label == now->tm_min || track_len <= 0) {
        continue;
      }
      char text[4];
      snprintf(text, sizeof(text), "%02d", label);
      int32_t clearance_q4 = box_half_q4 + (numerals_width(text, minute_h) / 2 + 2) * 16;
      int32_t clearance = (int32_t)((int64_t)clearance_q4 * DIAL_FULL_TURN / track_len);
      int32_t turn = dial_clear_pointer(dial_minute_offset(label, now->tm_min), clearance);
#else
      if (!dial_minute_label_visible(label, now->tm_min)) {
        continue;
      }
      int32_t turn = dial_minute_angle(label, now->tm_min);
#endif
      prv_draw_label(fb, &dial, label, minute_text, turn, minute_h, GColorWhite);
    }
    for (int slot = 1; slot < DIAL_HOUR_SLOTS; slot++) {
      prv_draw_label(fb, &dial, dial_hour_label(now->tm_hour, slot, is_24h), hour_text,
                     dial_hour_angle(slot), hour_h, GColorWhite);
    }
    graphics_release_frame_buffer(ctx, fb);
  }

  // The pointer shield at 12 o'clock carries the current hour and minute.
  prv_fill_sector(ctx, &dial, rim, rim_in, SHIELD_HALF_ANGLE, COLOR_SHIELD_EDGE);
  prv_fill_sector(ctx, &dial, rim_in, hour_in, SHIELD_HALF_ANGLE, COLOR_SHIELD_HOUR);
  prv_fill_sector(ctx, &dial, hour_in, minute_in, MINUTE_BOX_HALF_ANGLE, COLOR_SHIELD_MINUTE);

  fb = s_numerals_ready ? prv_capture(ctx) : NULL;
  if (fb) {
    prv_draw_label(fb, &dial, dial_hour_label(now->tm_hour, 0, is_24h), hour_text, 0, hour_h,
                   GColorWhite);
    prv_draw_label(fb, &dial, now->tm_min, minute_text, 0, minute_h, GColorWhite);
    graphics_release_frame_buffer(ctx, fb);
  }

  prv_draw_center(ctx, bounds, now);
  if (dial_seconds_visible(s_seconds_mode, s_glance_active) && s_in_focus) {
    int32_t angle = dial_second_angle(now->tm_sec);
    int length = unit >= 100 ? 3 : 2;
    GPoint outer = prv_q4_to_px(prv_track_point_q4(&dial,
                                  dial_track_inset(minute_in, 1), angle));
    GPoint inner = prv_q4_to_px(prv_track_point_q4(&dial,
                                  dial_track_inset(minute_in, 1 + length), angle));
    graphics_context_set_stroke_color(ctx, COLOR_SHIELD_MINUTE);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, inner, outer);
  }
}

static bool prv_read_steps(void) {
  bool available = false;
  int32_t steps = 0;
#if defined(PBL_HEALTH)
  time_t now = time(NULL);
  HealthServiceAccessibilityMask access = health_service_metric_accessible(
      HealthMetricStepCount, time_start_of_today(), now);
  if (access & HealthServiceAccessibilityMaskAvailable) {
    steps = health_service_sum_today(HealthMetricStepCount);
    available = steps >= 0;
  }
#endif
  bool changed = available != s_steps_available || steps != s_steps;
  if ((!s_steps_initialized || s_steps_available) && !available) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "step data unavailable; enable Pebble Health");
  }
  s_steps_initialized = true;
  s_steps_available = available;
  s_steps = steps;
  return changed;
}

static void prv_tick(struct tm *tick_time, TimeUnits changed);

static void prv_subscribe_ticks(void) {
  bool second_ticks = s_in_focus && dial_seconds_visible(s_seconds_mode, s_glance_active);
  if (second_ticks == s_second_ticks) {
    return;
  }
  s_second_ticks = second_ticks;
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(second_ticks ? SECOND_UNIT : MINUTE_UNIT, prv_tick);
}

static void prv_tick(struct tm *tick_time, TimeUnits changed) {
  (void)tick_time;
  if (changed & MINUTE_UNIT) {
    prv_read_steps();
  }
  if (s_in_focus) {
    layer_mark_dirty(s_dial_layer);
  }
}

static void prv_battery(BatteryChargeState state) {
  (void)state;
  if (s_in_focus) {
    layer_mark_dirty(s_dial_layer);
  }
}

static void prv_glance_end(void *context) {
  (void)context;
  s_seconds_timer = NULL;
  s_glance_active = false;
  prv_subscribe_ticks();
  if (s_in_focus) {
    layer_mark_dirty(s_dial_layer);
  }
}

static void prv_cancel_glance(void) {
  if (s_seconds_timer) {
    app_timer_cancel(s_seconds_timer);
    s_seconds_timer = NULL;
  }
  s_glance_active = false;
}

static void prv_tap(AccelAxisType axis, int32_t direction) {
  (void)axis;
  (void)direction;
  if (s_seconds_mode != DialSecondsOnShake || !s_in_focus) {
    return;
  }
  prv_cancel_glance();
  s_seconds_timer = app_timer_register(SECONDS_GLANCE_MS, prv_glance_end, NULL);
  if (!s_seconds_timer) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "could not start seconds timer");
    s_glance_active = false;
  } else {
    s_glance_active = true;
  }
  prv_subscribe_ticks();
  layer_mark_dirty(s_dial_layer);
}

static void prv_update_seconds_mode(void) {
  prv_cancel_glance();
  accel_tap_service_unsubscribe();
  if (s_seconds_mode == DialSecondsOnShake) {
    accel_tap_service_subscribe(prv_tap);
  }
  prv_subscribe_ticks();
}

static void prv_focus(bool in_focus) {
  s_in_focus = in_focus;
  if (!in_focus) {
    prv_cancel_glance();
  } else {
    prv_read_steps();
    layer_mark_dirty(s_dial_layer);
  }
  prv_subscribe_ticks();
}

#if defined(PBL_HEALTH)
static void prv_health(HealthEventType event, void *context) {
  (void)context;
  if (event == HealthEventSignificantUpdate || event == HealthEventMovementUpdate) {
    if (prv_read_steps() && s_in_focus) {
      layer_mark_dirty(s_dial_layer);
    }
  }
}
#endif

static bool prv_integer_tuple(const Tuple *tuple) {
  return (tuple->type == TUPLE_INT || tuple->type == TUPLE_UINT)
      && tuple->length == sizeof(int32_t);
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  Tuple *seconds = dict_find(iter, MESSAGE_KEY_ShowSeconds);
  Tuple *goal = dict_find(iter, MESSAGE_KEY_StepGoal);
  if (seconds && (!prv_integer_tuple(seconds) || seconds->value->uint32 > DialSecondsOnShake)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "invalid ShowSeconds setting");
    return;
  }
  if (goal && (!prv_integer_tuple(goal) || !dial_step_goal_valid(goal->value->int32))) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "invalid StepGoal setting");
    return;
  }
  if (seconds) {
    s_seconds_mode = (DialSecondsMode)seconds->value->uint32;
    // Keep key 1 compatible with the original persisted bool (0 or 1).
    if (persist_write_int(SETTINGS_KEY_SECONDS, s_seconds_mode) < 0) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "could not save ShowSeconds");
    }
    prv_update_seconds_mode();
  }
  if (goal) {
    s_step_goal = goal->value->int32;
    if (persist_write_int(SETTINGS_KEY_STEP_GOAL, s_step_goal) < 0) {
      APP_LOG(APP_LOG_LEVEL_ERROR, "could not save StepGoal");
    }
  }
  if (s_in_focus && (seconds || goal)) {
    layer_mark_dirty(s_dial_layer);
  }
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_ERROR, "settings message dropped: %d", reason);
}

static void prv_error_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "ROTO360 ERROR\nRESTART WATCHFACE",
                     fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(bounds.origin.x, bounds.origin.y + bounds.size.h / 2 - 20,
                           bounds.size.w, 40),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void prv_init_error(const char *reason) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "watchface initialization failed: %s", reason);
  s_init_failed = true;
  if (s_window) {
    Layer *root = window_get_root_layer(s_window);
    layer_set_update_proc(root, prv_error_update);
    layer_mark_dirty(root);
  }
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_dial_layer = layer_create(layer_get_bounds(root));
  if (!s_dial_layer) {
    prv_init_error("dial layer allocation");
    return;
  }
  layer_set_update_proc(s_dial_layer, prv_dial_update);
  layer_add_child(root, s_dial_layer);
}

static void prv_window_unload(Window *window) {
  (void)window;
  if (s_dial_layer) {
    layer_destroy(s_dial_layer);
    s_dial_layer = NULL;
  }
}

static bool prv_init(void) {
  s_window = window_create();
  if (!s_window) {
    prv_init_error("window allocation");
    return false;
  }
  s_numerals_ready = numerals_load();
  if (!s_numerals_ready) {
    prv_init_error("digit atlas load");
    window_stack_push(s_window, false);
    return false;
  }
  s_center_font = fonts_load_custom_font(resource_get_handle(CENTER_FONT));
  if (!s_center_font || s_center_font == fonts_get_system_font(FONT_KEY_FONT_FALLBACK)) {
    s_center_font = NULL;
    prv_init_error("center font load");
    window_stack_push(s_window, false);
    return false;
  }

  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, false);
  if (s_init_failed) {
    return false;
  }

  if (persist_exists(SETTINGS_KEY_SECONDS)) {
    int size = persist_get_size(SETTINGS_KEY_SECONDS);
    int32_t mode = -1;
    if (size == (int)sizeof(uint8_t)) {
      uint8_t legacy = 0;
      if (persist_read_data(SETTINGS_KEY_SECONDS, &legacy, sizeof(legacy)) == (int)sizeof(legacy)) {
        mode = legacy;
      }
    } else if (size == (int)sizeof(int32_t)) {
      mode = persist_read_int(SETTINGS_KEY_SECONDS);
    }
    if (mode >= DialSecondsOff && mode <= DialSecondsOnShake) {
      s_seconds_mode = (DialSecondsMode)mode;
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "invalid saved seconds mode: %ld", (long)mode);
    }
  }
  if (persist_exists(SETTINGS_KEY_STEP_GOAL)) {
    int32_t goal = persist_read_int(SETTINGS_KEY_STEP_GOAL);
    if (dial_step_goal_valid(goal)) {
      s_step_goal = goal;
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "invalid saved step goal: %ld", (long)goal);
    }
  }
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  AppMessageResult result = app_message_open(128, 64);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "could not open settings messaging: %d", result);
  }
  prv_read_steps();
#if defined(PBL_HEALTH)
  s_health_subscribed = health_service_events_subscribe(prv_health, NULL);
  if (!s_health_subscribed) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "could not subscribe to Health updates");
  }
#endif
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);
  prv_update_seconds_mode();
  app_focus_service_subscribe(prv_focus);
  battery_state_service_subscribe(prv_battery);
  s_services_started = true;
  return true;
}

static void prv_deinit(void) {
  if (s_services_started) {
    tick_timer_service_unsubscribe();
    prv_cancel_glance();
    accel_tap_service_unsubscribe();
    app_focus_service_unsubscribe();
#if defined(PBL_HEALTH)
    if (s_health_subscribed) {
      health_service_events_unsubscribe();
      s_health_subscribed = false;
    }
#endif
    battery_state_service_unsubscribe();
    app_message_deregister_callbacks();
    s_services_started = false;
  }
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
  if (s_center_font) {
    fonts_unload_custom_font(s_center_font);
    s_center_font = NULL;
  }
  numerals_unload();
  s_numerals_ready = false;
}

int main(void) {
  bool ready = prv_init();
  if (s_window) {
    app_event_loop();
  }
  prv_deinit();
  return ready ? 0 : 1;
}
