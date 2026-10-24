#pragma once

// Host declarations for the SDK surface used by main.c, not a rendering emulator.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PBL_DISPLAY_WIDTH
#define PBL_DISPLAY_WIDTH 260
#endif
#ifdef PBL_ROUND
#define PBL_IF_ROUND_ELSE(round, rect) (round)
#else
#define PBL_IF_ROUND_ELSE(round, rect) (rect)
#endif
#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xffff
#define DEG_TO_TRIGANGLE(degrees) ((degrees) * TRIG_MAX_ANGLE / 360)
#define RESOURCE_ID_FONT_BEBAS_12 12
#define RESOURCE_ID_FONT_BEBAS_14 14
#define RESOURCE_ID_FONT_BEBAS_18 18
#define RESOURCE_ID_FONT_BEBAS_20 20
#define RESOURCE_ID_DIGITS 100
#define MESSAGE_KEY_ShowSeconds 0
#define MESSAGE_KEY_StepGoal 1
#define FONT_KEY_FONT_FALLBACK "RESOURCE_ID_GOTHIC_14"
#define FONT_KEY_GOTHIC_14 "RESOURCE_ID_GOTHIC_14"
#define FONT_KEY_GOTHIC_14_BOLD "RESOURCE_ID_GOTHIC_14_BOLD"
#define FONT_KEY_GOTHIC_18 "RESOURCE_ID_GOTHIC_18"
#define FONT_KEY_GOTHIC_18_BOLD "RESOURCE_ID_GOTHIC_18_BOLD"
#define FONT_KEY_GOTHIC_24_BOLD "RESOURCE_ID_GOTHIC_24_BOLD"

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GPoint(x, y) ((GPoint){ (x), (y) })
#define GRect(x, y, w, h) ((GRect){ { (x), (y) }, { (w), (h) } })
typedef union { uint8_t argb; } GColor;
#define GColorBlack ((GColor){ .argb = 0xc0 })
#define GColorWhite ((GColor){ .argb = 0xff })
#define GColorDarkGray ((GColor){ .argb = 0xd5 })
#define GColorCobaltBlue ((GColor){ .argb = 0xc6 })
#define GColorOxfordBlue ((GColor){ .argb = 0xc1 })
#define GColorGreen ((GColor){ .argb = 0xcc })
#define GColorOrange ((GColor){ .argb = 0xf8 })
#define GColorRed ((GColor){ .argb = 0xf0 })
typedef enum { GCornerNone = 0, GCornersAll = 0xf } GCornerMask;
typedef enum { GOvalScaleModeFitCircle } GOvalScaleMode;
typedef enum { GTextOverflowModeWordWrap, GTextOverflowModeTrailingEllipsis,
               GTextOverflowModeFill } GTextOverflowMode;
typedef enum { GTextAlignmentLeft, GTextAlignmentCenter, GTextAlignmentRight } GTextAlignment;
typedef enum { GBitmapFormat1Bit, GBitmapFormat8Bit, GBitmapFormat8BitCircular } GBitmapFormat;
typedef struct GContext { int unused; } GContext;
typedef struct GBitmap GBitmap;
typedef struct { uint8_t *data; int16_t min_x, max_x; } GBitmapDataRowInfo;
typedef struct GTextAttributes GTextAttributes;
typedef struct FontInfo { int kind; } FontInfo;
typedef FontInfo *GFont;
typedef void *ResHandle;
typedef struct { uint32_t num_points; GPoint *points; } GPathInfo;
typedef struct GPath { int unused; } GPath;
typedef struct Layer Layer;
typedef struct Window Window;
typedef struct AppTimer AppTimer;
typedef void (*LayerUpdateProc)(Layer *, GContext *);
typedef void (*WindowHandler)(Window *);
typedef struct { WindowHandler load, appear, disappear, unload; } WindowHandlers;
typedef enum { SECOND_UNIT = 1, MINUTE_UNIT = 2, HOUR_UNIT = 4, DAY_UNIT = 8 } TimeUnits;
typedef void (*TickHandler)(struct tm *, TimeUnits);
typedef struct { uint8_t charge_percent; bool is_charging, is_plugged; } BatteryChargeState;
typedef void (*BatteryStateHandler)(BatteryChargeState);
typedef void (*AppFocusHandler)(bool);
typedef enum { ACCEL_AXIS_X, ACCEL_AXIS_Y, ACCEL_AXIS_Z } AccelAxisType;
typedef void (*AccelTapHandler)(AccelAxisType, int32_t);
typedef void (*AppTimerCallback)(void *);
typedef enum { APP_MSG_OK = 0, APP_MSG_OUT_OF_MEMORY = 1 << 10 } AppMessageResult;
typedef enum { TUPLE_BYTE_ARRAY, TUPLE_CSTRING, TUPLE_UINT, TUPLE_INT } TupleType;
typedef union { int32_t int32; uint32_t uint32; } TupleValue;
typedef struct { uint32_t key; TupleType type; uint16_t length; TupleValue *value; } Tuple;
typedef struct DictionaryIterator { Tuple *seconds, *goal; } DictionaryIterator;
typedef void (*AppMessageInboxReceived)(DictionaryIterator *, void *);
typedef void (*AppMessageInboxDropped)(AppMessageResult, void *);
typedef enum { APP_LOG_LEVEL_ERROR = 1, APP_LOG_LEVEL_WARNING = 50,
               APP_LOG_LEVEL_INFO = 100 } AppLogLevel;
void fake_app_log(AppLogLevel level, const char *format, ...);
#define APP_LOG(level, ...) fake_app_log(level, __VA_ARGS__)

Window *window_create(void);
void window_destroy(Window *);
void window_set_background_color(Window *, GColor);
void window_set_window_handlers(Window *, WindowHandlers);
void window_stack_push(Window *, bool);
Layer *window_get_root_layer(const Window *);
Layer *layer_create(GRect);
void layer_destroy(Layer *);
GRect layer_get_bounds(const Layer *);
void layer_set_update_proc(Layer *, LayerUpdateProc);
void layer_add_child(Layer *, Layer *);
void layer_mark_dirty(Layer *);
GPoint grect_center_point(const GRect *);
GFont fonts_load_custom_font(ResHandle);
GFont fonts_get_system_font(const char *);
void fonts_unload_custom_font(GFont);
ResHandle resource_get_handle(uint32_t);
size_t resource_size(ResHandle);
size_t resource_load(ResHandle, uint8_t *, size_t);
void tick_timer_service_subscribe(TimeUnits, TickHandler);
void tick_timer_service_unsubscribe(void);
void battery_state_service_subscribe(BatteryStateHandler);
void battery_state_service_unsubscribe(void);
BatteryChargeState battery_state_service_peek(void);
void app_focus_service_subscribe(AppFocusHandler);
void app_focus_service_unsubscribe(void);
void accel_tap_service_subscribe(AccelTapHandler);
void accel_tap_service_unsubscribe(void);
AppTimer *app_timer_register(uint32_t, AppTimerCallback, void *);
void app_timer_cancel(AppTimer *);
bool persist_exists(int);
int persist_get_size(int);
int persist_read_data(int, void *, size_t);
int32_t persist_read_int(int);
int persist_write_int(int, int32_t);
Tuple *dict_find(const DictionaryIterator *, uint32_t);
AppMessageInboxReceived app_message_register_inbox_received(AppMessageInboxReceived);
AppMessageInboxDropped app_message_register_inbox_dropped(AppMessageInboxDropped);
AppMessageResult app_message_open(uint32_t, uint32_t);
void app_message_deregister_callbacks(void);
void app_event_loop(void);
bool clock_is_24h_style(void);
int32_t sin_lookup(int32_t);
int32_t cos_lookup(int32_t);
void graphics_context_set_fill_color(GContext *, GColor);
void graphics_context_set_stroke_color(GContext *, GColor);
void graphics_context_set_text_color(GContext *, GColor);
void graphics_context_set_stroke_width(GContext *, uint8_t);
void graphics_context_set_antialiased(GContext *, bool);
void graphics_fill_rect(GContext *, GRect, uint16_t, GCornerMask);
void graphics_fill_circle(GContext *, GPoint, uint16_t);
void graphics_fill_radial(GContext *, GRect, GOvalScaleMode, uint16_t, int32_t, int32_t);
void graphics_draw_line(GContext *, GPoint, GPoint);
void graphics_draw_text(GContext *, const char *, GFont, GRect, GTextOverflowMode,
                        GTextAlignment, GTextAttributes *);
GPath *gpath_create(const GPathInfo *);
void gpath_draw_filled(GContext *, GPath *);
void gpath_destroy(GPath *);
GBitmap *graphics_capture_frame_buffer(GContext *);
void graphics_release_frame_buffer(GContext *, GBitmap *);
GBitmapFormat gbitmap_get_format(const GBitmap *);
GRect gbitmap_get_bounds(const GBitmap *);
GBitmapDataRowInfo gbitmap_get_data_row_info(const GBitmap *, int16_t);

#ifdef PBL_HEALTH
typedef int32_t HealthValue;
typedef enum { HealthMetricStepCount } HealthMetric;
typedef enum { HealthServiceAccessibilityMaskNotAvailable = 0,
               HealthServiceAccessibilityMaskAvailable = 1 } HealthServiceAccessibilityMask;
typedef enum { HealthEventSignificantUpdate, HealthEventMovementUpdate,
               HealthEventSleepUpdate } HealthEventType;
typedef void (*HealthEventHandler)(HealthEventType, void *);
HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric, time_t, time_t);
HealthValue health_service_sum_today(HealthMetric);
bool health_service_events_subscribe(HealthEventHandler, void *);
bool health_service_events_unsubscribe(void);
time_t time_start_of_today(void);
#endif
