'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.join(__dirname, '..');

test('numeral resources reject short reads and malformed data without leaks', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'roto-numerals-'));
  const binary = path.join(dir, 'numerals_test');
  try {
    execFileSync(process.env.CC || 'cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror',
      '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
      '-I', path.join(root, 'tests/native'),
      '-o', binary, path.join(root, 'tests/native/numerals_test.c')
    ], { stdio: 'inherit' });
    const output = execFileSync(binary, [
      path.join(root, 'app/resources/data/digits.bin')
    ], { encoding: 'utf8' });
    assert.match(output, /numerals fault tests passed/);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
