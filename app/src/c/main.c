#include <pebble.h>

#include "dial.h"
#include "numerals.h"

#if PBL_DISPLAY_WIDTH >= 260
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_22
#define CENTER_LINE 24
#elif PBL_DISPLAY_WIDTH >= 200
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_20
#define CENTER_LINE 21
#else
#define CENTER_FONT RESOURCE_ID_FONT_BEBAS_16
#define CENTER_LINE 17
#endif

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
  int font_h = CENTER_LINE + 6;
  for (int i = 0; i < 3; i++) {
    // The font's cap height sits low in its line box, so nudge each line up.
    int y = center.y + (i - 1) * CENTER_LINE - font_h / 2 - CENTER_LINE / 5;
    graphics_context_set_text_color(ctx, colors[i]);
    graphics_draw_text(ctx, lines[i], s_center_font, GRect(bounds.origin.x, y, box_w, font_h),
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
}

static void prv_tick(struct tm *tick_time, TimeUnits changed) {
  (void)tick_time;
  (void)changed;
  layer_mark_dirty(s_dial_layer);
}

static void prv_battery(BatteryChargeState state) {
  (void)state;
  layer_mark_dirty(s_dial_layer);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_dial_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_dial_layer, prv_dial_update);
  layer_add_child(root, s_dial_layer);
}

static void prv_window_unload(Window *window) {
  (void)window;
  layer_destroy(s_dial_layer);
}

static void prv_init(void) {
  s_numerals_ready = numerals_load();
  if (!s_numerals_ready) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "digit atlas failed to load");
  }
  s_center_font = fonts_load_custom_font(resource_get_handle(CENTER_FONT));

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, false);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);
  battery_state_service_subscribe(prv_battery);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_center_font);
  numerals_unload();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
