#include "numerals.h"

#define GLYPH_COUNT 10
#define MAX_LABEL_DIGITS 4
#define HEADER_SIZE 8
#define TABLE_ENTRY_SIZE 8

typedef struct {
  uint16_t width;
  uint16_t stride;
  const uint8_t *rows;
} Glyph;

static uint8_t *s_blob;
static Glyph s_glyphs[GLYPH_COUNT];
static int s_height;

static uint32_t prv_read_le(const uint8_t *p, int bytes) {
  uint32_t value = 0;
  for (int i = bytes - 1; i >= 0; i--) {
    value = (value << 8) | p[i];
  }
  return value;
}

bool numerals_load(void) {
  ResHandle handle = resource_get_handle(RESOURCE_ID_DIGITS);
  size_t size = resource_size(handle);
  size_t table_end = HEADER_SIZE + GLYPH_COUNT * TABLE_ENTRY_SIZE;
  if (size <= table_end) {
    return false;
  }
  s_blob = malloc(size);
  if (!s_blob) {
    return false;
  }
  resource_load(handle, s_blob, size);
  if (memcmp(s_blob, "RD01", 4) != 0 || s_blob[4] != GLYPH_COUNT) {
    numerals_unload();
    return false;
  }
  s_height = (int)prv_read_le(s_blob + 6, 2);
  const uint8_t *data = s_blob + table_end;
  size_t data_size = size - table_end;
  for (int i = 0; i < GLYPH_COUNT; i++) {
    const uint8_t *entry = s_blob + HEADER_SIZE + i * TABLE_ENTRY_SIZE;
    uint16_t width = (uint16_t)prv_read_le(entry, 2);
    uint16_t stride = (uint16_t)prv_read_le(entry + 2, 2);
    uint32_t offset = prv_read_le(entry + 4, 4);
    if (stride * 8 < width || offset + (size_t)stride * s_height > data_size) {
      numerals_unload();
      return false;
    }
    s_glyphs[i] = (Glyph){ .width = width, .stride = stride, .rows = data + offset };
  }
  return true;
}

void numerals_unload(void) {
  free(s_blob);
  s_blob = NULL;
}

// Blends two 2-bit-per-channel colours; `coverage` is out of 9 samples.
static uint8_t prv_blend(uint8_t bg, uint8_t fg, int coverage) {
  uint8_t out = 0xC0;
  for (int shift = 0; shift < 6; shift += 2) {
    int b = (bg >> shift) & 3;
    int f = (fg >> shift) & 3;
    out |= (uint8_t)(((b * (9 - coverage) + f * coverage + 4) / 9) << shift);
  }
  return out;
}

void numerals_draw(GBitmap *fb, const char *digits, GPoint center_q4, int32_t angle,
                   int digit_height, GColor color) {
  if (!s_blob || !fb || digit_height <= 0) {
    return;
  }

  // Lay the label out in atlas pixels.
  const Glyph *glyphs[MAX_LABEL_DIGITS];
  int starts[MAX_LABEL_DIGITS];
  int count = 0;
  int gap = s_height * 8 / 100;
  int width = 0;
  for (const char *c = digits; *c && count < MAX_LABEL_DIGITS; c++) {
    if (*c < '0' || *c > '9') {
      continue;
    }
    if (count > 0) {
      width += gap;
    }
    glyphs[count] = &s_glyphs[*c - '0'];
    starts[count] = width;
    width += glyphs[count]->width;
    count++;
  }
  if (count == 0) {
    return;
  }

  // Atlas pixels per screen pixel, in 16.16 fixed point.
  int32_t scale = (int32_t)s_height * 65536 / digit_height;
  int32_t cos_a = cos_lookup(angle);
  int32_t sin_a = sin_lookup(angle);
  int half_w = width / 2;
  int half_h = s_height / 2;
  int reach = (width + s_height) * digit_height / (2 * s_height) + 2;
  int cx = center_q4.x >> 4;
  int cy = center_q4.y >> 4;

  GRect fb_bounds = gbitmap_get_bounds(fb);
  int y0 = cy - reach < 0 ? 0 : cy - reach;
  int y1 = cy + reach >= fb_bounds.size.h ? fb_bounds.size.h - 1 : cy + reach;

  // 3x3 supersampling grid offsets inside a pixel, in 1/16 pixel units.
  static const int8_t offsets[3] = { -5, 0, 5 };
  for (int y = y0; y <= y1; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    int x0 = cx - reach < row.min_x ? row.min_x : cx - reach;
    int x1 = cx + reach > row.max_x ? row.max_x : cx + reach;
    for (int x = x0; x <= x1; x++) {
      int coverage = 0;
      for (int sy = 0; sy < 3; sy++) {
        int32_t dy = y * 16 + 8 + offsets[sy] - center_q4.y;
        for (int sx = 0; sx < 3; sx++) {
          int32_t dx = x * 16 + 8 + offsets[sx] - center_q4.x;
          int32_t u = (dx * cos_a + dy * sin_a) >> 16;
          int32_t v = (dy * cos_a - dx * sin_a) >> 16;
          int su = (int)((u * scale) >> 20) + half_w;
          int sv = (int)((v * scale) >> 20) + half_h;
          if (su < 0 || su >= width || sv < 0 || sv >= s_height) {
            continue;
          }
          for (int g = count - 1; g >= 0; g--) {
            if (su >= starts[g]) {
              int gx = su - starts[g];
              if (gx < glyphs[g]->width &&
                  (glyphs[g]->rows[sv * glyphs[g]->stride + (gx >> 3)] & (0x80 >> (gx & 7)))) {
                coverage++;
              }
              break;
            }
          }
        }
      }
      if (coverage > 0) {
        row.data[x] = coverage == 9 ? color.argb : prv_blend(row.data[x], color.argb, coverage);
      }
    }
  }
}
