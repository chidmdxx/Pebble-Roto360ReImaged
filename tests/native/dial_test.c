// Host tests for app/src/c/dial.c. Built and run by tests/dial.test.js.
#include "../../app/src/c/dial.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
  if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
    failures++; \
  } \
} while (0)

#define CHECK_STR(actual, expected) do { \
  if (strcmp((actual), (expected)) != 0) { \
    fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
    failures++; \
  } \
} while (0)

static void test_hour_labels(void) {
  CHECK(dial_hour_label(8, 0, true) == 8);
  CHECK(dial_hour_label(23, 1, true) == 0);
  CHECK(dial_hour_label(0, -1, true) == 23);
  CHECK(dial_hour_label(0, 0, false) == 12);
  CHECK(dial_hour_label(12, 0, false) == 12);
  CHECK(dial_hour_label(13, 0, false) == 1);
  CHECK(dial_hour_label(11, 1, false) == 12);
  for (int h = 0; h < 24; h++) {
    for (int slot = -6; slot <= 6; slot++) {
      int label = dial_hour_label(h, slot, false);
      CHECK(label >= 1 && label <= 12);
    }
  }
}

static void test_minute_ring(void) {
  CHECK(dial_minute_label(0) == 0);
  CHECK(dial_minute_label(11) == 55);
  CHECK(dial_minute_label(12) == 0);
  CHECK(dial_minute_label(-1) == 55);
  CHECK(dial_minute_angle(20, 20) == 0);
  CHECK(dial_minute_angle(35, 20) == DIAL_FULL_TURN / 4);
  CHECK(dial_minute_angle(5, 20) == DIAL_FULL_TURN * 3 / 4);
  CHECK(dial_hour_angle(3) == DIAL_FULL_TURN / 4);
  CHECK(dial_hour_angle(12) == 0);

  // Labels within two minutes of the pointer would sit half under its box.
  CHECK(!dial_minute_label_visible(20, 20));
  CHECK(!dial_minute_label_visible(25, 23));
  CHECK(dial_minute_label_visible(25, 22));
  CHECK(!dial_minute_label_visible(0, 59));
  CHECK(!dial_minute_label_visible(55, 57));
  CHECK(dial_minute_label_visible(55, 58));
  CHECK(dial_minute_label_visible(50, 57));
}

static void test_text(void) {
  char buf[16];
  dial_format_battery(buf, sizeof buf, 80, false);
  CHECK_STR(buf, "80%");
  dial_format_battery(buf, sizeof buf, 80, true);
  CHECK_STR(buf, "+80%");
  dial_format_battery(buf, sizeof buf, 140, false);
  CHECK_STR(buf, "100%");
  dial_format_battery(buf, sizeof buf, -5, false);
  CHECK_STR(buf, "0%");
  dial_format_date(buf, sizeof buf, 10, 8);
  CHECK_STR(buf, "NOV 08");
  CHECK_STR(dial_day_name(3), "WEDNESDAY");
  CHECK_STR(dial_day_name(7), "");
  CHECK_STR(dial_month_name(-1), "");
}

static int32_t fixed(double value) {
  return (int32_t)lround(value * DIAL_FULL_TURN);
}

static int32_t distance_at(DialTrack track, double degrees) {
  double rad = degrees * M_PI / 180.0;
  return dial_track_distance_q4(track, fixed(sin(rad)), fixed(cos(rad)));
}

static void test_track(void) {
  DialTrack circle = { 130, 130, 130 };
  for (int deg = 0; deg < 360; deg += 7) {
    CHECK(abs(distance_at(circle, deg) - 130 * 16) <= 2);
  }

  DialTrack box = { 100, 114, 0 };
  CHECK(distance_at(box, 0) == 114 * 16);
  CHECK(distance_at(box, 90) == 100 * 16);
  CHECK(distance_at(box, 180) == 114 * 16);

  // Rounded corners pull the diagonal in, never out.
  DialTrack rounded = { 100, 114, 42 };
  double diagonal = atan2(100, 114) * 180.0 / M_PI;
  CHECK(distance_at(rounded, diagonal) < distance_at(box, diagonal));
  CHECK(distance_at(rounded, 0) == 114 * 16);

  DialTrack inset = dial_track_inset(rounded, 10);
  CHECK(inset.half_w == 90 && inset.half_h == 104 && inset.corner == 32);
  DialTrack tiny = dial_track_inset(rounded, 200);
  CHECK(tiny.half_w == 0 && tiny.half_h == 0 && tiny.corner == 0);
  CHECK(dial_track_distance_q4(rounded, 0, 0) == 0);
}

int main(void) {
  test_hour_labels();
  test_minute_ring();
  test_text();
  test_track();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("dial tests passed");
  return 0;
}
