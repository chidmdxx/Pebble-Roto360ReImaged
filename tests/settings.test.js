'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../app/src/pkjs/index.js'), 'utf8');

function companion(initial = {}) {
  const storage = new Map(Object.entries(initial));
  const events = {};
  const messages = [];
  const notifications = [];
  const urls = [];
  const context = {
    Pebble: {
      addEventListener: (name, callback) => { events[name] = callback; },
      sendAppMessage: (message, success, failure) => { messages.push({ message, success, failure }); },
      showSimpleNotificationOnPebble: (title, message) => { notifications.push(message); },
      openURL: url => { urls.push(url); }
    },
    localStorage: {
      getItem: key => storage.has(key) ? storage.get(key) : null,
      setItem: (key, value) => { storage.set(key, value); }
    },
    console: { log() {} }
  };
  vm.runInNewContext(source, context);
  return { events, messages, notifications, urls, storage, context };
}

function page(phone) {
  phone.events.showConfiguration();
  return decodeURIComponent(phone.urls[phone.urls.length - 1].split(',')[1]);
}

function settings(mode, goal = 8000, disableSteps = false) {
  return { ShowSeconds: mode, StepGoal: goal, DisableSteps: disableSteps };
}

test('configuration declares Health and preserves existing message key order', () => {
  const app = require('../app/package.json');
  assert.deepEqual(app.pebble.capabilities, ['configurable', 'health']);
  assert.deepEqual(app.pebble.messageKeys, ['ShowSeconds', 'StepGoal', 'DisableSteps']);
  assert.equal(app.version, require('../package.json').version);
  const phone = companion();
  phone.events.ready();
  assert.equal(phone.messages.length, 0);
  const html = page(phone);
  assert.match(html, /value="1" selected>Always/);
  assert.match(html, /required value="8000"/);
  assert.match(html, /10 seconds after wrist shake/);
  assert.match(html, /id="disableSteps" type="checkbox">/);
});

test('embedded form submits all settings and rejects invalid goals', () => {
  const phone = companion();
  const html = page(phone);
  let closeUrl;
  const alerts = [];
  const elements = { seconds: { value: '2' }, goal: { value: '10000' },
    disableSteps: { checked: true }, settings: {} };
  vm.runInNewContext(html.match(/<script>(.*?)<\/script>/)[1], {
    document: { getElementById: id => elements[id] },
    window: { location: { set href(value) { closeUrl = value; } } },
    alert: message => alerts.push(message)
  });
  elements.settings.onsubmit({ preventDefault() {} });
  phone.events.webviewclosed({ response: closeUrl.split('#')[1] });
  assert.deepEqual(JSON.parse(phone.storage.get('settings')), settings(2, 10000, true));
  assert.equal(phone.messages[0].message.ShowSeconds, 2);
  assert.equal(phone.messages[0].message.StepGoal, 10000);
  assert.equal(phone.messages[0].message.DisableSteps, 1);
  elements.disableSteps.checked = false;
  elements.settings.onsubmit({ preventDefault() {} });
  phone.events.webviewclosed({ response: closeUrl.split('#')[1] });
  assert.equal(phone.messages[1].message.DisableSteps, 0);
  assert.deepEqual(JSON.parse(phone.storage.get('settings')), settings(2, 10000));
  for (const goal of ['', '0', '-1', '1.5', '100001', 'Infinity', 'bad']) {
    elements.goal.value = goal;
    closeUrl = null;
    elements.settings.onsubmit({ preventDefault() {} });
    assert.equal(closeUrl, null, goal);
  }
  assert.equal(alerts.length, 7);
});

test('all seconds modes, goal boundaries and step switches persist across response encodings', () => {
  for (const mode of [0, 1, 2]) {
    for (const goal of [1, 8000, 100000]) {
      for (const encode of [value => value, encodeURIComponent]) {
        for (const disabled of [false, true]) {
          const phone = companion();
          phone.events.webviewclosed({ response: encode(JSON.stringify(settings(mode, goal, disabled))) });
          assert.equal(phone.messages[0].message.ShowSeconds, mode);
          assert.equal(phone.messages[0].message.StepGoal, goal);
          assert.equal(phone.messages[0].message.DisableSteps, Number(disabled));
          assert.deepEqual(JSON.parse(phone.storage.get('settings')), settings(mode, goal, disabled));
          assert.equal(phone.notifications.length, 0);
        }
      }
    }
  }
});

test('saved settings sync on reconnect and populate all controls', () => {
  for (const mode of [0, 1, 2]) {
    const phone = companion({ settings: JSON.stringify(settings(mode, 12000, true)) });
    phone.events.ready();
    assert.equal(phone.messages[0].message.ShowSeconds, mode);
    assert.equal(phone.messages[0].message.StepGoal, 12000);
    assert.equal(phone.messages[0].message.DisableSteps, 1);
    const html = page(phone);
    assert.ok(html.includes('value="' + mode + '" selected'));
    assert.match(html, /required value="12000"/);
    assert.match(html, /id="disableSteps" type="checkbox" checked>/);
  }
});

test('old on/off preferences migrate without changing the selected behavior', () => {
  for (const legacy of ['0', '1']) {
    const phone = companion({ showSeconds: legacy });
    phone.events.ready();
    assert.equal(phone.messages[0].message.ShowSeconds, Number(legacy));
    assert.equal(phone.messages[0].message.StepGoal, 8000);
    assert.equal(phone.messages[0].message.DisableSteps, 0);
    assert.ok(page(phone).includes('value="' + legacy + '" selected'));
  }
  const phone = companion({ showSeconds: '1', settings: JSON.stringify(settings(2, 4000)) });
  phone.events.ready();
  assert.equal(phone.messages[0].message.ShowSeconds, 2);
  const previous = companion({ settings: JSON.stringify({ ShowSeconds: 0, StepGoal: 4000 }) });
  previous.events.ready();
  assert.equal(previous.messages[0].message.DisableSteps, 0);
  assert.equal(previous.messages[0].message.ShowSeconds, 0);
  assert.equal(previous.messages[0].message.StepGoal, 4000);
  assert.match(page(previous), /id="disableSteps" type="checkbox">/);
});

test('cancel and malformed settings do not change stored preferences', () => {
  const original = JSON.stringify(settings(1));
  const phone = companion({ settings: original });
  for (const response of ['', 'CANCELLED']) {
    phone.events.webviewclosed({ response });
  }
  assert.equal(phone.notifications.length, 0);
  const invalid = ['%', '{}', 'null', '{"ShowSeconds":true,"StepGoal":8000}',
    ...[-1, 3, 1.5, '1', null].map(mode => JSON.stringify(settings(mode))),
    ...[0, -1, 100001, 1.5, '8000', null].map(goal => JSON.stringify(settings(1, goal))),
    ...[0, 1, 'true', null].map(disabled => JSON.stringify(settings(1, 8000, disabled))),
    '{"ShowSeconds":1,"StepGoal":8000}'];
  for (const response of invalid) {
    phone.events.webviewclosed({ response });
  }
  assert.equal(phone.notifications.length, invalid.length);
  assert.equal(phone.messages.length, 0);
  assert.equal(phone.storage.get('settings'), original);
});

test('delivery, corrupt storage and write failures are reported', () => {
  const phone = companion({ settings: JSON.stringify(settings(0)) });
  phone.events.ready();
  phone.messages[0].failure('disconnected');
  assert.match(phone.notifications[0], /Could not update the watch/);
  for (const initial of [{ showSeconds: 'invalid' }, { settings: 'invalid' },
    { settings: JSON.stringify(settings(4)) }]) {
    const corrupt = companion(initial);
    corrupt.events.ready();
    corrupt.events.showConfiguration();
    assert.equal(corrupt.notifications.length, 2);
    assert.equal(corrupt.messages.length, 0);
    assert.equal(corrupt.urls.length, 0);
  }
  phone.context.localStorage.setItem = () => { throw new Error('storage full'); };
  phone.events.webviewclosed({ response: JSON.stringify(settings(2)) });
  assert.equal(phone.messages.length, 1);
  assert.match(phone.notifications[1], /Could not save settings/);
});
