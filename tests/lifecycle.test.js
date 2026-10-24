'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { execFileSync, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const scenarios = [
  'window-failure', 'layer-failure', 'font-null', 'font-fallback', 'atlas-failure',
  'main', 'always', 'off', 'shake', 'timer-failure', 'focus', 'cleanup',
  'health', 'health-subscription-failure', 'message-failure', 'partial-cleanup'
];

test('native watchface lifecycle and fault injection', async t => {
  const compiler = process.env.CC || 'cc';
  const useWsl = process.platform === 'win32' && !process.env.CC
    && spawnSync(compiler, ['--version']).error;
  const executable = useWsl ? 'wsl.exe' : compiler;
  const prefix = useWsl ? ['--exec', 'cc'] : [];
  // Keep build products in this checkout, including when Windows uses WSL's compiler.
  const dir = fs.mkdtempSync(path.join(__dirname, '.lifecycle-build-'));
  try {
    for (const [platform, defines] of [
      ['round-health', ['-DPBL_DISPLAY_WIDTH=260', '-DPBL_ROUND', '-DPBL_HEALTH']],
      ['rect-no-health', ['-DPBL_DISPLAY_WIDTH=144']]
    ]) {
      const binary = path.relative(root, path.join(dir, platform))
        .split(path.sep).join('/');
      execFileSync(executable, [...prefix,
        '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
        ...defines, '-Itests/native', '-o', binary,
        'tests/native/lifecycle_test.c', 'app/src/c/dial.c', '-lm'
      ], { cwd: root, stdio: 'inherit' });
      for (const scenario of scenarios) {
        await t.test(`${platform}: ${scenario}`, () => {
          const output = useWsl
            ? execFileSync('wsl.exe', ['--exec', `./${binary}`, scenario],
              { cwd: root, encoding: 'utf8' })
            : execFileSync(path.join(root, binary), [scenario],
              { cwd: root, encoding: 'utf8' });
          assert.match(output, /lifecycle test passed/);
        });
      }
    }
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
