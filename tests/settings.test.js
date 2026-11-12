'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../app/src/pkjs/index.js'), 'utf8');

function companion(initial = {}) {
  const storage = new Map(Object.entries(initial)), events = {}, messages = [], notifications = [],
    urls = [], subscriptions = [];
  const context = {
    Pebble: {
      addEventListener: (name, callback) => { events[name] = callback; },
      sendAppMessage: (message, success, failure) => {
        messages.push({ message, success, failure }); success();
      },
      subscribeToSource: options => {
        subscriptions.push(options);
        return { unsubscribe: () => { options.unsubscribed = true; } };
      },
      showSimpleNotificationOnPebble: (title, message) => { notifications.push(message); },
      openURL: url => { urls.push(url); }
    },
    localStorage: {
      getItem: key => storage.has(key) ? storage.get(key) : null,
      setItem: (key, value) => { storage.set(key, value); }
    },
    console: { log() {} },
    atob: value => Buffer.from(value, 'base64').toString('binary')
  };
  vm.runInNewContext(source, context);
  return { events, messages, notifications, urls, storage, subscriptions, context };
}

function settings(mode = 1, goal = 8000, disableSteps = false, order = [0]) {
  return { ShowSeconds: mode, StepGoal: goal, DisableSteps: disableSteps, CenterOrder: order };
}
function reading(category, item, property, index = 0) {
  const text = value => ({ shortText: { text: value } });
  return { properties: { category: text(category), item: text(item), property: text(property),
    index: { numericValue: { value: index } } } };
}
function previewPhone(order = [5, 0]) {
  const phone = companion({ settings: JSON.stringify(settings(1, 8000, false, order)) });
  phone.events.ready();
  return phone;
}
function setPreferred(phone, ...entries) {
  phone.subscriptions[0].onData({ instances: entries });
}
function last(phone) { return phone.messages.at(-1).message; }
function page(phone) {
  phone.events.showConfiguration();
  return decodeURIComponent(phone.urls.at(-1).split(',')[1]);
}

test('default configuration preserves battery and never subscribes to plugins', () => {
  const app = require('../app/package.json');
  assert.deepEqual(app.pebble.capabilities, ['configurable', 'health']);
  assert.deepEqual(app.pebble.messageKeys.slice(0, 3), ['ShowSeconds', 'StepGoal', 'DisableSteps']);
  assert.equal(app.version, require('../package.json').version);
  const phone = companion();
  phone.events.ready();
  phone.events.appmessage({ payload: { RequestWeather: 1 } });
  const html = page(phone);
  assert.match(html, /value="1" selected>Always/);
  assert.match(html, /required value="8000"/);
  assert.match(html, /id="disableSteps" type="checkbox">/);
  assert.match(html, /id="preview" type="checkbox">/);
  assert.match(html, /Enable plugin complications \(preview; off by default\)/);
  assert.match(html, /When plugins are off or unavailable/);
  assert.match(html, /value="0" selected>Battery percentage/);
  assert.equal(phone.messages.length, 0);
  assert.equal(phone.subscriptions.length, 0);
});

test('disabling plugins restores battery and stops subscriptions across restart', () => {
  const phone = previewPhone();
  setPreferred(phone, reading('weather', 'location', 'temperature'));
  const preferences = phone.subscriptions[0], weather = phone.subscriptions[1];
  phone.events.webviewclosed({ response: JSON.stringify(settings()) });
  assert.equal(last(phone).CenterOrder, 1);
  assert.equal(preferences.unsubscribed, true);
  assert.equal(weather.unsubscribed, true);
  const count = phone.messages.length;
  weather.onData({ instances: [{ properties: {
    temperature: { numericValue: { value: 20, unit: '°C' } }
  } }] });
  phone.events.appmessage({ payload: { RequestWeather: 1 } });
  assert.equal(phone.messages.length, count);
  const restarted = companion({ settings: phone.storage.get('settings') });
  restarted.events.ready();
  assert.equal(last(restarted).CenterOrder, 1);
  assert.equal(restarted.subscriptions.length, 0);
  assert.match(page(restarted), /id="preview" type="checkbox">/);
});

test('embedded form submits settings and rejects invalid goals', () => {
  const phone = companion(), html = page(phone), alerts = [];
  let closeUrl;
  const elements = { seconds: { value: '2' }, goal: { value: '10000' },
    disableSteps: { checked: true }, preview: { checked: true }, fallback: { value: '1' }, settings: {} };
  vm.runInNewContext(html.match(/<script>(.*?)<\/script>/)[1], {
    document: { getElementById: id => elements[id] },
    window: { location: { set href(value) { closeUrl = value; } } },
    alert: message => alerts.push(message)
  });
  elements.settings.onsubmit({ preventDefault() {} });
  phone.events.webviewclosed({ response: closeUrl.split('#')[1] });
  assert.deepEqual(JSON.parse(phone.storage.get('settings')), settings(2, 10000, true, [5, 1]));
  assert.equal(phone.messages[0].message.CenterOrder, 6 | (2 << 3));
  assert.equal(phone.subscriptions[0].category, 'preferences');
  for (const goal of ['', '0', '-1', '1.5', '100001', 'Infinity', 'bad']) {
    elements.goal.value = goal; closeUrl = null;
    elements.settings.onsubmit({ preventDefault() {} });
    assert.equal(closeUrl, null, goal);
  }
  assert.equal(alerts.length, 7);
});

test('settings boundaries, response encodings and legacy seconds migrate', () => {
  for (const mode of [0, 1, 2]) for (const goal of [1, 8000, 100000]) {
    for (const encode of [value => value, encodeURIComponent]) for (const disabled of [false, true]) {
      const phone = companion();
      phone.events.webviewclosed({ response: encode(JSON.stringify(settings(mode, goal, disabled))) });
      assert.equal(phone.messages[0].message.ShowSeconds, mode);
      assert.equal(phone.messages[0].message.StepGoal, goal);
      assert.equal(phone.messages[0].message.DisableSteps, Number(disabled));
      assert.deepEqual(JSON.parse(phone.storage.get('settings')), settings(mode, goal, disabled));
      assert.equal(phone.notifications.length, 0);
    }
  }
  for (const legacy of ['0', '1']) {
    const phone = companion({ showSeconds: legacy });
    phone.events.ready();
    assert.equal(phone.messages[0].message.ShowSeconds, Number(legacy));
    assert.equal(phone.messages[0].message.CenterOrder, 1);
  }
  const previous = companion({ settings: JSON.stringify({ ShowSeconds: 0, StepGoal: 4000 }) });
  previous.events.ready();
  assert.equal(previous.messages[0].message.DisableSteps, 0);
});

test('invalid settings, storage and delivery failures are reported', () => {
  const original = JSON.stringify(settings()), phone = companion({ settings: original });
  for (const response of ['', 'CANCELLED']) phone.events.webviewclosed({ response });
  const invalid = ['%', '{}', 'null',
    ...[-1, 3, 1.5, '1', null].map(mode => JSON.stringify(settings(mode))),
    ...[0, -1, 100001, 1.5, '8000', null].map(goal => JSON.stringify(settings(1, goal))),
    ...[0, 1, 'true', null].map(disabled => JSON.stringify(settings(1, 8000, disabled))),
    ...[[], [2], [5], [5, 2], [0, 1], [5, 0, 1], [5, 5]].map(order =>
      JSON.stringify(settings(1, 8000, false, order)))];
  invalid.forEach(response => phone.events.webviewclosed({ response }));
  assert.equal(phone.notifications.length, invalid.length);
  assert.equal(phone.messages.length, 0);
  assert.equal(phone.storage.get('settings'), original);
  const corrupt = companion({ settings: 'invalid' });
  corrupt.events.ready(); corrupt.events.showConfiguration();
  assert.equal(corrupt.notifications.length, 2);
  phone.context.Pebble.sendAppMessage = (message, success, failure) => failure('disconnected');
  phone.events.ready();
  assert.match(phone.notifications.at(-1), /Could not update the watch/);
  phone.context.localStorage.setItem = () => { throw new Error('storage full'); };
  phone.events.webviewclosed({ response: JSON.stringify(settings()) });
  assert.match(phone.notifications.at(-1), /Could not save settings/);
});

test('previous source lists migrate to shared order with their terminal fallback', () => {
  for (const [old, expected] of [[[2, 3, 0], [5, 0]], [[4], [5, 1]], [[0, 2], [0]], [[1], [1]]]) {
    const phone = companion({ settings: JSON.stringify(settings(1, 8000, false, old)) });
    phone.events.ready();
    assert.equal(phone.messages[0].message.CenterOrder,
      expected.reduce((sum, value, index) => sum | ((value + 1) << (3 * index)), 0));
    assert.equal(phone.subscriptions.length, expected[0] === 5 ? 1 : 0);
  }
  const phone = companion({ settings: JSON.stringify({
    ShowSeconds: 1, StepGoal: 8000, DisableSteps: false, CenterDisplay: 2
  }) });
  phone.events.ready();
  assert.equal(phone.messages[0].message.CenterOrder, 6 | (2 << 3));
});

test('shared preferences subscribe in order, preserving properties and instance indices', () => {
  const phone = previewPhone();
  assert.equal(phone.subscriptions[0].category, 'preferences');
  assert.equal(phone.subscriptions[0].item, 'complications');
  setPreferred(phone, reading('music', 'track', 'artist', 1),
    reading('weather', 'location', 'temperature', 2),
    reading('finance', 'stock', 'price', 0),
    reading('notifications', 'notification', 'body', 7));
  assert.deepEqual(phone.subscriptions.slice(1).map(s => s.category),
    ['music', 'weather', 'finance', 'notifications']);
  assert.deepEqual(Array.from(phone.subscriptions[1].properties), ['artist', 'playing']);
  phone.subscriptions[1].onData({ instances: [
    { properties: { artist: { longText: { text: 'Wrong' } }, playing: { boolean: { value: true } } } },
    { properties: { artist: { longText: { text: 'Correct' } }, playing: { boolean: { value: true } } } }
  ] });
  assert.equal(last(phone).SlotIndex, 0);
  assert.equal(last(phone).SlotText, 'Correct');
  phone.subscriptions[3].onData({ instances: [{ properties: {
    price: { numericValue: { value: 42, unit: '%' } }
  } }] });
  assert.equal(last(phone).SlotIndex, 2);
  assert.equal(last(phone).SlotText, '42%');
  phone.subscriptions[4].onData({ instances: [{}, {}] });
  assert.equal(last(phone).SlotIndex, 3);
  assert.equal(last(phone).SlotKind, 3);
  assert.equal(last(phone).SlotText, '2');
});

test('weather uses chosen instance and units and forwards Pebble condition pixels', () => {
  const phone = previewPhone();
  setPreferred(phone, reading('weather', 'location', 'temperature', 1));
  const source = phone.subscriptions[1], pixels = Buffer.alloc(50, 0x12).toString('base64'),
    palette = Buffer.alloc(3, 0xff).toString('base64'), validUntilMs = Date.now() + 600000;
  source.onData({ validUntilMs, instances: [{}, { properties: {
    temperature: { numericValue: { value: -3.4, unit: '°C' } },
    condition: { image: { width: 10, height: 10, pixels, palette } }
  } }] });
  assert.equal(last(phone).SlotText, '-3 C');
  assert.equal(last(phone).SlotKind, 2);
  assert.equal(last(phone).SlotExpires, Math.floor(validUntilMs / 1000));
  assert.deepEqual(Array.from(last(phone).WeatherPixels), Array(50).fill(0x12));
  assert.deepEqual(Array.from(last(phone).WeatherPalette), [255, 255, 255, ...Array(13).fill(0)]);
  source.onData({ instances: [{}, { properties: { temperature: { numericValue: { value: 72, unit: '°F' } },
    condition: { image: { width: 10, height: 10, pixels, palette: Buffer.alloc(1).toString('base64') } }
  } }] });
  assert.equal(last(phone).SlotText, '72 F');
  assert.equal(last(phone).WeatherPixels, undefined);
  source.onData({ instances: [{}, { properties: {
    temperature: { numericValue: { value: 280, unit: 'K' } }
  } }] });
  assert.equal(last(phone).SlotKind, 0);
});

test('paused music, zero notifications, missing data, errors and expiry clear slots', () => {
  const phone = previewPhone();
  setPreferred(phone, reading('music', 'track', 'title'), reading('notifications', 'notification', 'name'));
  const music = phone.subscriptions[1], notifications = phone.subscriptions[2];
  music.onData({ instances: [{ properties: {
    title: { shortText: { text: 'Short' }, longText: { text: 'é'.repeat(60) } },
    playing: { boolean: { value: true } }
  } }] });
  assert.equal(last(phone).SlotText, 'Short');
  music.onData({ instances: [{ properties: {
    title: { shortText: { text: ' ' }, longText: { text: 'é'.repeat(60) } },
    playing: { boolean: { value: true } }
  } }] });
  assert.equal(last(phone).SlotText, 'é'.repeat(36));
  music.onData({ instances: [{ properties: {
    title: { shortText: { text: 7 }, longText: { text: 'Fallback' } },
    playing: { boolean: { value: true } }
  } }] });
  assert.equal(last(phone).SlotText, 'Fallback');
  music.onData({ instances: [{ properties: {
    title: { longText: { text: 'Paused' } }, playing: { boolean: { value: false } }
  } }] });
  assert.equal(last(phone).SlotKind, 0);
  notifications.onData({ instances: [] });
  assert.equal(last(phone).SlotKind, 0);
  music.onData({ validUntilMs: 1, instances: [{ properties: {
    title: { longText: { text: 'Expired' } }, playing: { boolean: { value: true } }
  } }] });
  assert.equal(last(phone).SlotKind, 0);
  music.onError({ code: 'PERMISSION_DENIED', message: 'denied' });
  assert.equal(last(phone).SlotKind, 0);
  notifications.onData({ instances: [{}] });
  notifications.onError({ code: 'PERMISSION_DENIED', message: 'denied' });
  assert.equal(last(phone).SlotKind, 0);
});

test('preference changes and opt-out unsubscribe and ignore late callbacks', () => {
  const phone = previewPhone(), preferences = phone.subscriptions[0];
  setPreferred(phone, reading('music', 'track', 'title'));
  const old = phone.subscriptions[1];
  preferences.onData({ instances: [reading('finance', 'stock', 'price')] });
  assert.equal(old.unsubscribed, true);
  const after = phone.messages.length;
  old.onData({ instances: [] });
  assert.equal(phone.messages.length, after);
  phone.events.webviewclosed({ response: JSON.stringify(settings()) });
  assert.equal(preferences.unsubscribed, true);
  assert.equal(phone.subscriptions[2].unsubscribed, true);
  phone.events.webviewclosed({ response: JSON.stringify(settings(1, 8000, false, [5, 0])) });
  const before = phone.subscriptions.length;
  preferences.onData({ instances: [reading('weather', 'location', 'temperature')] });
  assert.equal(phone.subscriptions.length, before);
});

test('reconnect sends cached slots, and preference errors clear stale data', () => {
  const phone = previewPhone();
  setPreferred(phone, reading('finance', 'stock', 'price'));
  phone.subscriptions[1].onData({ instances: [{ properties: { price: { shortText: { text: '42' } } } }] });
  phone.events.appmessage({ payload: { RequestWeather: 1 } });
  assert.equal(phone.messages.at(-2).message.SlotReset, 1);
  assert.equal(last(phone).SlotText, '42');
  phone.subscriptions[0].onError({ code: 'UNAVAILABLE', message: 'failed' });
  assert.equal(last(phone).SlotReset, 1);
  assert.equal(phone.subscriptions[1].unsubscribed, true);
});

test('unsupported and inaccessible readings fall through and at most five are subscribed', () => {
  const phone = previewPhone();
  setPreferred(phone, ...Array.from({ length: 6 }, (_, index) =>
    reading('finance', 'stock', 'price', index)));
  assert.equal(phone.subscriptions.length, 6);
  phone.subscriptions[1].onData({ instances: [{ properties: { price: { image: {} } } }] });
  assert.equal(last(phone).SlotKind, 0);
  phone.subscriptions[2].onData({ instances: [] });
  assert.equal(last(phone).SlotKind, 0);
  phone.subscriptions[3].onError({ code: 'PERMISSION_DENIED', message: 'denied' });
  assert.equal(last(phone).SlotKind, 0);
});

test('settings and source messages remain ordered across delayed deliveries', () => {
  const phone = companion({ settings: JSON.stringify(settings(1, 8000, false, [5, 0])) });
  phone.context.Pebble.sendAppMessage = (message, success, failure) => {
    phone.messages.push({ message, success, failure });
  };
  phone.events.ready();
  assert.equal(phone.messages.length, 1);
  phone.messages[0].success();
  assert.equal(phone.messages[1].message.SlotReset, 1);
  setPreferred(phone, reading('music', 'track', 'title'));
  phone.subscriptions[1].onData({ instances: [{ properties: {
    title: { longText: { text: 'Track' } }, playing: { boolean: { value: true } }
  } }] });
  phone.events.webviewclosed({ response: JSON.stringify(settings()) });
  assert.equal(phone.messages.length, 2);
  phone.messages[1].success();
  assert.equal(phone.messages[2].message.SlotReset, 1);
  phone.messages[2].success();
  assert.equal(phone.messages[3].message.SlotText, 'Track');
  phone.messages[3].success();
  assert.equal(phone.messages[4].message.CenterOrder, 1);
});
