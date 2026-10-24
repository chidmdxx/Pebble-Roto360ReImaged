#include <pebble.h>

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *fixture;
static size_t fixture_size;
static size_t loaded_size;
static bool fail_allocation;
static int allocations;

static void *test_malloc(size_t size) {
  if (fail_allocation) {
    return NULL;
  }
  void *data = malloc(size);
  if (data) {
    allocations++;
  }
  return data;
}

static void test_free(void *data) {
  if (data) {
    allocations--;
  }
  free(data);
}

#define malloc test_malloc
#define free test_free
#ifndef NUMERALS_SOURCE
#define NUMERALS_SOURCE "../../app/src/c/numerals.c"
#endif
#include NUMERALS_SOURCE
#undef malloc
#undef free

ResHandle resource_get_handle(uint32_t id) {
  (void)id;
  return (ResHandle)fixture;
}

size_t resource_size(ResHandle handle) {
  (void)handle;
  return fixture_size;
}

size_t resource_load(ResHandle handle, uint8_t *buffer, size_t length) {
  (void)handle;
  size_t count = loaded_size < length ? loaded_size : length;
  memcpy(buffer, fixture, count);
  return count;
}

static void write_le(uint8_t *data, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; i++) {
    data[i] = (uint8_t)(value >> (i * 8));
  }
}

static void check_rejected(void) {
  assert(!numerals_load());
  assert(allocations == 0);
  assert(numerals_width("12", 20) == 0);
  numerals_unload();
  assert(allocations == 0);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  assert(fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  assert(length > 88);
  fixture_size = loaded_size = (size_t)length;
  fixture = malloc(fixture_size);
  uint8_t *original = malloc(fixture_size);
  assert(fixture && original);
  rewind(file);
  assert(fread(original, 1, fixture_size, file) == fixture_size);
  fclose(file);
  memcpy(fixture, original, fixture_size);

  assert(numerals_load());
  assert(allocations == 1);
  assert(numerals_width("12", 20) > 0);
  numerals_unload();
  numerals_unload();
  assert(allocations == 0);

  loaded_size--;
  check_rejected();
  loaded_size = 0;
  check_rejected();
  loaded_size = fixture_size;
  fail_allocation = true;
  check_rejected();
  fail_allocation = false;

  const struct {
    int offset;
    uint32_t value;
    int bytes;
  } invalid[] = {
    { 0, 0, 4 },                 // magic
    { 4, 9, 1 },                 // digit count
    { 5, 1, 1 },                 // reserved header byte
    { 6, 0, 2 },                 // height
    { 6, UINT16_MAX, 2 },        // rows exceed resource
    { 8, 0, 2 },                 // width
    { 10, 0, 2 },                // stride
    { 10, UINT16_MAX, 2 },       // oversized stride
    { 12, UINT32_MAX, 4 },       // out-of-range offset
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    memcpy(fixture, original, fixture_size);
    write_le(fixture + invalid[i].offset, invalid[i].value, invalid[i].bytes);
    check_rejected();
  }

  memcpy(fixture, original, fixture_size);
  size_t full_size = fixture_size;
  fixture_size = 88;
  check_rejected();
  fixture_size = full_size - 1;
  check_rejected();
  fixture_size = full_size;

  assert(numerals_load());
  assert(numerals_load());
  assert(allocations == 1);
  loaded_size = 0;
  check_rejected();
  free(original);
  free(fixture);
  puts("numerals fault tests passed");
  return 0;
}
