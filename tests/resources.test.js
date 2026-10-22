'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const app = require('../app/package.json');

const DISPLAYS = {
  basalt: [144, 168],
  chalk: [180, 180],
  emery: [200, 228],
  gabbro: [260, 260]
};

function png(file) {
  const image = fs.readFileSync(path.join(root, file));
  assert.equal(image.subarray(0, 8).toString('hex'), '89504e470d0a1a0a', file);
  assert.equal(image.toString('ascii', 12, 16), 'IHDR', file);
  return { width: image.readUInt32BE(16), height: image.readUInt32BE(20), colorType: image[25] };
}

test('the watchface targets color round and rectangular watches with its own UUID', () => {
  assert.deepEqual(app.pebble.targetPlatforms, Object.keys(DISPLAYS));
  assert.equal(app.pebble.watchapp.watchface, true);
  assert.equal(app.pebble.enableMultiJS, true);
  assert.equal(app.version, require('../package.json').version);
  assert.match(app.pebble.uuid, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
  // Must never reuse the original Roto360 UUID, or installing would replace it.
  assert.notEqual(app.pebble.uuid, '22dc1c24-0be2-450f-a3f1-e24ef60d0278');
});

test('every font resource uses the bundled OFL Bebas Neue', () => {
  const fonts = app.pebble.resources.media.filter(resource => resource.type === 'font');
  assert.ok(fonts.length > 0);
  const covered = new Set();
  for (const font of fonts) {
    assert.equal(font.file, 'fonts/BebasNeue-Regular.ttf');
    font.targetPlatforms.forEach(platform => covered.add(platform));
  }
  assert.deepEqual([...covered].sort(), Object.keys(DISPLAYS).sort());
  const license = fs.readFileSync(path.join(root, 'app/resources/fonts/OFL.txt'), 'utf8');
  assert.match(license, /SIL OPEN FONT LICENSE Version 1\.1/);
});

test('the digit atlas header and glyph table are consistent', () => {
  const atlas = fs.readFileSync(path.join(root, 'app/resources/data/digits.bin'));
  assert.equal(atlas.toString('ascii', 0, 4), 'RD01');
  const count = atlas[4];
  const height = atlas.readUInt16LE(6);
  assert.equal(count, 10);
  assert.ok(height > 0);
  const dataStart = 8 + count * 8;
  let expected = 0;
  for (let i = 0; i < count; i++) {
    const entry = 8 + i * 8;
    const width = atlas.readUInt16LE(entry);
    const stride = atlas.readUInt16LE(entry + 2);
    const offset = atlas.readUInt32LE(entry + 4);
    assert.ok(width > 0 && stride === Math.ceil(width / 8), `glyph ${i}`);
    assert.equal(offset, expected, `glyph ${i}`);
    expected += stride * height;
  }
  assert.equal(atlas.length, dataStart + expected);
});

test('store screenshots match each display exactly', () => {
  for (const [platform, [width, height]] of Object.entries(DISPLAYS)) {
    for (let i = 1; i <= 5; i++) {
      const image = png(`store/screenshots/${platform}-${i}.png`);
      assert.deepEqual([image.width, image.height], [width, height], `${platform}-${i}`);
    }
  }
});

test('store banners and icons have the required sizes', () => {
  for (const platform of Object.keys(DISPLAYS)) {
    const banner = png(`store/banner-${platform}.png`);
    assert.deepEqual([banner.width, banner.height], [720, 320], platform);
  }
  for (const size of [80, 144]) {
    const icon = png(`store/icon-${size}.png`);
    assert.deepEqual([icon.width, icon.height, icon.colorType], [size, size, 2]);
  }
});
