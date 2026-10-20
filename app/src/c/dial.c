#include "dial.h"

#include <stdio.h>

static const char *const DAYS[] = {
  "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"
};

static const char *const MONTHS[] = {
  "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

static int prv_mod(int value, int base) {
  int r = value % base;
  return r < 0 ? r + base : r;
}

int dial_hour_label(int hour24, int slot, bool is_24h) {
  int hour = prv_mod(hour24 + slot, 24);
  if (is_24h) {
    return hour;
  }
  int h12 = hour % 12;
  return h12 == 0 ? 12 : h12;
}

int dial_minute_label(int slot) {
  return prv_mod(slot, DIAL_MINUTE_SLOTS) * 5;
}

int32_t dial_minute_angle(int label_minute, int minute) {
  return (int32_t)prv_mod(label_minute - minute, 60) * DIAL_FULL_TURN / 60;
}

bool dial_minute_label_visible(int label_minute, int minute) {
  int diff = prv_mod(label_minute - minute, 60);
  return diff > 2 && diff < 58;
}

int32_t dial_hour_angle(int slot) {
  return (int32_t)prv_mod(slot, DIAL_HOUR_SLOTS) * DIAL_FULL_TURN / DIAL_HOUR_SLOTS;
}

const char *dial_day_name(int wday) {
  return (wday >= 0 && wday < 7) ? DAYS[wday] : "";
}

const char *dial_month_name(int mon) {
  return (mon >= 0 && mon < 12) ? MONTHS[mon] : "";
}

void dial_format_battery(char *buf, size_t len, int percent, bool charging) {
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  snprintf(buf, len, charging ? "+%d%%" : "%d%%", percent);
}

void dial_format_date(char *buf, size_t len, int mon, int mday) {
  snprintf(buf, len, "%s %02d", dial_month_name(mon), mday);
}

DialTrack dial_track_inset(DialTrack base, int inset) {
  DialTrack t = { base.half_w - inset, base.half_h - inset, base.corner - inset };
  if (t.half_w < 0) {
    t.half_w = 0;
  }
  if (t.half_h < 0) {
    t.half_h = 0;
  }
  if (t.corner < 0) {
    t.corner = 0;
  }
  return t;
}

static int64_t prv_isqrt(int64_t value) {
  if (value <= 0) {
    return 0;
  }
  int64_t x = value;
  int64_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + value / x) / 2;
  }
  return x;
}

int32_t dial_track_distance_q4(DialTrack track, int32_t sin_a, int32_t cos_a) {
  const int64_t one = DIAL_FULL_TURN;
  int64_t dx = sin_a < 0 ? -(int64_t)sin_a : sin_a;
  int64_t dy = cos_a < 0 ? -(int64_t)cos_a : cos_a;
  int64_t a = track.half_w;
  int64_t b = track.half_h;
  int64_t rc = track.corner;
  if (rc > a) {
    rc = a;
  }
  if (rc > b) {
    rc = b;
  }

  // Distance to the straight edges of the bounding box.
  int64_t t = INT64_MAX;
  if (dx > 0) {
    t = a * one * 16 / dx;
  }
  if (dy > 0 && b * one * 16 / dy < t) {
    t = b * one * 16 / dy;
  }
  if (t == INT64_MAX) {
    return 0;
  }

  int64_t x = t * dx / one;
  int64_t y = t * dy / one;
  if (rc == 0 || x <= (a - rc) * 16 || y <= (b - rc) * 16) {
    return (int32_t)t;
  }

  // The ray leaves through a rounded corner; intersect with its circle.
  int64_t cx = a - rc;
  int64_t cy = b - rc;
  int64_t d = dx * cx + dy * cy;
  int64_t disc = d * d - (cx * cx + cy * cy - rc * rc) * one * one;
  return (int32_t)((d + prv_isqrt(disc)) * 16 / one);
}
