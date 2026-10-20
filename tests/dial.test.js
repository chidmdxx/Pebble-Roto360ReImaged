'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.join(__dirname, '..');

test('dial logic passes the native host tests', () => {
  const compiler = process.env.CC || 'cc';
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'roto-dial-'));
  const binary = path.join(dir, 'dial_test');
  try {
    execFileSync(compiler, [
      '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
      '-o', binary,
      path.join(root, 'tests/native/dial_test.c'),
      path.join(root, 'app/src/c/dial.c'),
      '-lm'
    ], { stdio: 'inherit' });
    const output = execFileSync(binary, { encoding: 'utf8' });
    assert.match(output, /dial tests passed/);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
