// Host tests for app/src/c/dial.c. Built and run by tests/dial.test.js.
#include "../../app/src/c/dial.h"

#include <math.h>
#include <limits.h>
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
  CHECK(dial_minute_offset(5, 3) == DIAL_FULL_TURN * 2 / 60);
  CHECK(dial_minute_offset(0, 3) == dial_minute_angle(0, 3) - DIAL_FULL_TURN);
  CHECK(dial_minute_offset(0, 3) < 0);
  CHECK(dial_minute_offset(33, 3) == DIAL_FULL_TURN / 2);
  CHECK(dial_minute_offset(35, 3) < 0);
  CHECK(dial_clear_pointer(100, 3000) == 3000);
  CHECK(dial_clear_pointer(-100, 3000) == -3000);
  CHECK(dial_clear_pointer(5000, 3000) == 5000);
  CHECK(dial_clear_pointer(-5000, 3000) == -5000);
  CHECK(dial_clear_pointer(0, 3000) == 0);
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

static void test_center_layout(void) {
  // SDK-rendered Bebas capitals: height and WEDNESDAY advance at each font size.
  const struct {
    DialTrack center;
    int cap_height;
    int weekday_width;
  } displays[] = {
    { { 28, 40, 8 }, 9, 44 },
    { { 36, 36, 36 }, 10, 53 },
    { { 40, 54, 12 }, 13, 67 },
    { { 52, 52, 52 }, 14, 73 },
  };
  for (size_t i = 0; i < sizeof displays / sizeof displays[0]; i++) {
    int height = displays[i].cap_height;
    int top = dial_center_line_top(height, 2, 0);
    int bottom = dial_center_line_top(height, 2, 2) + height;
    CHECK(abs(top + bottom) <= 1);
    CHECK(dial_center_line_top(height, 2, 1) - top == height + 2);
    // Keep the longest weekday's upper corners clear of the seconds track.
    double x = (displays[i].weekday_width + 1) / 2;
    double y = top;
    double distance = sqrt(x * x + y * y);
    double angle = atan2(x, -y) * 180.0 / M_PI;
    CHECK(distance * 16 < distance_at(dial_track_inset(displays[i].center, 4), angle));
  }
}

static void test_seconds(void) {
  CHECK(dial_second_angle(0) == 0);
  CHECK(dial_second_angle(15) == DIAL_FULL_TURN / 4);
  CHECK(dial_second_angle(30) == DIAL_FULL_TURN / 2);
  CHECK(dial_second_angle(45) == DIAL_FULL_TURN * 3 / 4);
  CHECK(dial_second_angle(60) == 0);
  CHECK(dial_second_angle(-1) == dial_second_angle(59));
  for (int second = 1; second < 60; second++) {
    CHECK(dial_second_angle(second) > dial_second_angle(second - 1));
  }
  CHECK(!dial_seconds_visible(DialSecondsOff, false));
  CHECK(!dial_seconds_visible(DialSecondsOff, true));
  CHECK(dial_seconds_visible(DialSecondsAlways, false));
  CHECK(dial_seconds_visible(DialSecondsAlways, true));
  CHECK(!dial_seconds_visible(DialSecondsOnShake, false));
  CHECK(dial_seconds_visible(DialSecondsOnShake, true));
  CHECK(!dial_seconds_visible((DialSecondsMode)3, true));
}

static void test_step_progress(void) {
  const int32_t full = DIAL_FULL_TURN * 330 / 360;
  CHECK(dial_step_goal_valid(1));
  CHECK(dial_step_goal_valid(DIAL_STEP_GOAL_DEFAULT));
  CHECK(dial_step_goal_valid(DIAL_STEP_GOAL_MAX));
  CHECK(!dial_step_goal_valid(0));
  CHECK(!dial_step_goal_valid(-1));
  CHECK(!dial_step_goal_valid(DIAL_STEP_GOAL_MAX + 1));
  CHECK(!dial_step_goal_valid(INT32_MAX));
  CHECK(dial_step_progress(0, 8000) == 0);
  CHECK(dial_step_progress(-100, 8000) == 0);
  CHECK(dial_step_progress(4000, 8000) == full / 2);
  CHECK(dial_step_progress(8000, 8000) == full);
  CHECK(dial_step_progress(16000, 8000) == full);
  CHECK(dial_step_progress(INT32_MAX, 8000) == full);
  CHECK(dial_step_progress(1, 1) == full);
  CHECK(dial_step_progress(1, 0) == 0);
  CHECK(dial_step_progress(1, -1) == 0);
  CHECK(dial_step_progress(100, DIAL_STEP_GOAL_MAX + 1) == 0);
  for (int goal = 1; goal <= DIAL_STEP_GOAL_MAX; goal += 997) {
    int32_t previous = 0;
    for (int steps = 0; steps <= goal * 2; steps += 31) {
      int32_t progress = dial_step_progress(steps, goal);
      CHECK(progress >= previous && progress <= full);
      previous = progress;
    }
  }
  // Health's daily total resets at midnight; the arc must reset with it.
  CHECK(dial_step_progress(8000, 8000) > dial_step_progress(0, 8000));
}

int main(void) {
  test_hour_labels();
  test_minute_ring();
  test_text();
  test_track();
  test_center_layout();
  test_seconds();
  test_step_progress();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  puts("dial tests passed");
  return 0;
}
