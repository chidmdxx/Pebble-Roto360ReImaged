#pragma once
// Pure dial logic with no Pebble SDK dependency, so tests can build it on a host.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One full turn, matching the SDK's TRIG_MAX_ANGLE.
#define DIAL_FULL_TURN 0x10000

#define DIAL_HOUR_SLOTS 12
#define DIAL_MINUTE_SLOTS 12

// Hour shown at ring slot `slot` (0 is the top, increasing clockwise).
// Returns 0-23 in 24-hour mode and 1-12 in 12-hour mode.
int dial_hour_label(int hour24, int slot, bool is_24h);

// Minute value printed at minute ring slot `slot` (0, 5, ... 55).
int dial_minute_label(int slot);

// Clockwise angle of a minute ring label when `minute` sits under the pointer.
int32_t dial_minute_angle(int label_minute, int minute);

// False for ring labels close enough to the pointer box to be half covered.
bool dial_minute_label_visible(int label_minute, int minute);

// Clockwise angle of hour ring slot `slot`.
int32_t dial_hour_angle(int slot);

// Upper-case names for the centre readout. Out-of-range input returns "".
const char *dial_day_name(int wday);
const char *dial_month_name(int mon);

// Writes "80%" or, while charging, "+80%". Percent is clamped to 0-100.
void dial_format_battery(char *buf, size_t len, int percent, bool charging);

// Writes "NOV 08".
void dial_format_date(char *buf, size_t len, int mon, int mday);

// A ring track: a rounded rectangle centred on the dial. A circle of radius r
// is the track {r, r, r}, so round and rectangular screens share one layout.
typedef struct {
  int half_w;
  int half_h;
  int corner;
} DialTrack;

// The track `inset` pixels inside `base`, keeping the rings concentric.
DialTrack dial_track_inset(DialTrack base, int inset);

// Distance from the centre to `track` along the direction given by sin/cos
// (16.16 fixed point, 0 pointing up, clockwise), in 1/16 pixel units.
int32_t dial_track_distance_q4(DialTrack track, int32_t sin_a, int32_t cos_a);
