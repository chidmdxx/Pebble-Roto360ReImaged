'use strict';

var storageKey = 'settings';
var defaultGoal = 8000;
var maxGoal = 100000;
var messageQueue = [];
var sending = false;
var previewEnabled = false;
var generation = 0;
var preferencesGeneration = 0;
var preferencesSubscription = null;
var sourceSubscriptions = [];
var slotMessages = [];

function reportError(message, error) {
  console.log(message + ': ' + String(error));
  Pebble.showSimpleNotificationOnPebble('Roto360 settings', message);
}

function sendNext() {
  if (sending || !messageQueue.length) return;
  sending = true;
  var entry = messageQueue[0];
  Pebble.sendAppMessage(entry.message, function() {
    messageQueue.shift();
    sending = false;
    sendNext();
  }, function(error) {
    messageQueue.shift();
    sending = false;
    if (entry.settings) {
      reportError('Could not update the watch. Reconnect and save settings again.', error);
    } else {
      console.log('Could not send center display data: ' + String(error));
    }
    sendNext();
  });
}

function enqueueMessage(message, settings) {
  messageQueue.push({ message: message, settings: settings });
  sendNext();
}

function expiry(update) {
  return update && typeof update.validUntilMs === 'number' && isFinite(update.validUntilMs) &&
    update.validUntilMs > 0 && update.validUntilMs < 2147483647000
    ? Math.max(1, Math.floor(update.validUntilMs / 1000)) : 0;
}

function textOf(property) {
  var short = property && property.shortText;
  if (short && typeof short.text === 'string' && short.text.trim()) return short.text;
  var shape = property && property.longText;
  return shape && typeof shape.text === 'string' ? shape.text : '';
}

function safeText(value) {
  var text = value.replace(/\s+/g, ' ').replace(/[\u0000-\u001f\u007f]/g, '').trim();
  var chars = text.match(/[\uD800-\uDBFF][\uDC00-\uDFFF]|[^\uD800-\uDFFF]/g) || [];
  var result = '';
  chars.some(function(character) {
    if (encodeURIComponent(result + character).replace(/%[0-9A-F]{2}/gi, 'x').length > 72) {
      return true;
    }
    result += character;
    return false;
  });
  return result;
}

function iconBytes(encoded) {
  var binary = atob(encoded);
  var bytes = [];
  for (var i = 0; i < binary.length; i++) bytes.push(binary.charCodeAt(i));
  return bytes;
}

function addWeatherIcon(message, image) {
  if (!image || image.width !== 10 || image.height !== 10) return;
  try {
    var pixels = iconBytes(image.pixels);
    var palette = iconBytes(image.palette);
    if (pixels.length !== 50 || !palette.length || palette.length > 16 ||
        !pixels.every(function(byte) {
          return (byte >> 4) < palette.length && (byte & 15) < palette.length;
        })) {
      console.log('Invalid weather icon');
      return;
    }
    while (palette.length < 16) palette.push(0);
    message.WeatherPixels = pixels;
    message.WeatherPalette = palette;
  } catch (error) {
    console.log('Invalid weather icon: ' + String(error));
  }
}

function clearSlot(index) {
  return { SlotIndex: index, SlotKind: 0, SlotText: '', SlotExpires: 0 };
}

function sendSlot(index, message) {
  if (message.SlotExpires && Date.now() >= message.SlotExpires * 1000) message = clearSlot(index);
  slotMessages[index] = message;
  enqueueMessage(message, false);
}

function readingMessage(index, reading, update) {
  var result = clearSlot(index);
  result.SlotExpires = expiry(update);
  var instances = update && Array.isArray(update.instances) ? update.instances : [];
  if (reading.category === 'notifications' && reading.item === 'notification') {
    if (instances.length) {
      result.SlotKind = 3;
      result.SlotText = String(Math.min(instances.length, 999));
    }
    return result;
  }
  var instance = instances[reading.index];
  var props = instance && instance.properties;
  if (!props) return result;
  if (reading.category === 'music' && reading.item === 'track' &&
      !(props.playing && props.playing.boolean && props.playing.boolean.value === true)) return result;
  var property = props[reading.property];
  if (!property) return result;
  var numeric = property.numericValue;
  var temperature = reading.category === 'weather' && reading.item === 'location' &&
    ['temperature', 'feels_like', 'high', 'low'].indexOf(reading.property) >= 0;
  if (temperature && (!numeric || typeof numeric.value !== 'number' || !isFinite(numeric.value) ||
      (numeric.unit !== '°C' && numeric.unit !== '°F') ||
      numeric.value < -99 || numeric.value > 999)) return result;
  if (reading.category === 'weather' && reading.item === 'location' && numeric &&
      typeof numeric.value === 'number' && isFinite(numeric.value) &&
      (numeric.unit === '°C' || numeric.unit === '°F') &&
      numeric.value >= -99 && numeric.value <= 999) {
    result.SlotKind = 2;
    result.SlotText = Math.round(numeric.value) + (numeric.unit === '°C' ? ' C' : ' F');
    addWeatherIcon(result, props.condition && props.condition.image);
  } else {
    var text = textOf(property);
    if (!text && numeric && typeof numeric.value === 'number' && isFinite(numeric.value)) {
      text = String(numeric.value) + (typeof numeric.unit === 'string' ? numeric.unit : '');
    }
    result.SlotText = safeText(text);
    if (result.SlotText) result.SlotKind = 1;
  }
  return result;
}

function unsubscribeSources() {
  sourceSubscriptions.forEach(function(subscription) { subscription.unsubscribe(); });
  sourceSubscriptions = [];
}

function resetSlots() {
  slotMessages = [];
  enqueueMessage({ SlotReset: 1 }, false);
}

function subscribeReading(reading, index, activeGeneration) {
  var properties = [reading.property];
  if (reading.category === 'music' && reading.item === 'track' &&
      properties.indexOf('playing') < 0) properties.push('playing');
  if (reading.category === 'weather' && reading.item === 'location' &&
      properties.indexOf('condition') < 0) properties.push('condition');
  if (reading.category === 'notifications' && reading.item === 'notification') properties = ['package'];
  try {
    sourceSubscriptions.push(Pebble.subscribeToSource({
      category: reading.category, item: reading.item, properties: properties,
      iconPixelSize: { w: 10, h: 10 },
      onData: function(update) {
        if (previewEnabled && activeGeneration === generation) {
          sendSlot(index, readingMessage(index, reading, update));
        }
      },
      onError: function(error) {
        if (previewEnabled && activeGeneration === generation) {
          console.log('Complication unavailable: ' + error.code + ' ' + error.message);
          sendSlot(index, clearSlot(index));
        }
      }
    }));
  } catch (error) {
    console.log('Could not subscribe to complication: ' + String(error));
    sendSlot(index, clearSlot(index));
  }
}

function updatePreferred(update) {
  var activeGeneration = ++generation;
  unsubscribeSources();
  resetSlots();
  var instances = update && Array.isArray(update.instances) ? update.instances.slice(0, 5) : [];
  instances.forEach(function(instance, index) {
    var props = instance && instance.properties;
    var numeric = props && props.index && props.index.numericValue;
    var reading = {
      category: textOf(props && props.category), item: textOf(props && props.item),
      property: textOf(props && props.property), index: numeric ? numeric.value : 0
    };
    if (reading.category && reading.item && reading.property &&
        typeof reading.index === 'number' && isFinite(reading.index) &&
        reading.index >= 0 && reading.index % 1 === 0) {
      subscribeReading(reading, index, activeGeneration);
    }
  });
}

function updateSources(order) {
  var enabled = order[0] === 5;
  if (enabled === previewEnabled) return;
  previewEnabled = enabled;
  generation++;
  var activePreferences = ++preferencesGeneration;
  if (preferencesSubscription) preferencesSubscription.unsubscribe();
  preferencesSubscription = null;
  unsubscribeSources();
  slotMessages = [];
  if (!enabled) return;
  resetSlots();
  if (typeof Pebble.subscribeToSource !== 'function') {
    console.log('Plugin preview unavailable');
    return;
  }
  try {
    preferencesSubscription = Pebble.subscribeToSource({
      category: 'preferences', item: 'complications',
      onData: function(update) {
        if (previewEnabled && activePreferences === preferencesGeneration) updatePreferred(update);
      },
      onError: function(error) {
        if (!previewEnabled || activePreferences !== preferencesGeneration) return;
        console.log('Complication preferences unavailable: ' + error.code + ' ' + error.message);
        generation++;
        unsubscribeSources();
        resetSlots();
      }
    });
  } catch (error) {
    console.log('Could not subscribe to complication preferences: ' + String(error));
  }
}

function encodeOrder(order) {
  return order.reduce(function(packed, value, index) {
    return packed | ((value + 1) << (3 * index));
  }, 0);
}

function validateSettings(settings) {
  if (!settings || typeof settings.ShowSeconds !== 'number' ||
      settings.ShowSeconds % 1 !== 0 || settings.ShowSeconds < 0 || settings.ShowSeconds > 2 ||
      typeof settings.StepGoal !== 'number' || settings.StepGoal % 1 !== 0 ||
      settings.StepGoal < 1 || settings.StepGoal > maxGoal ||
      typeof settings.DisableSteps !== 'boolean' || !Array.isArray(settings.CenterOrder) ||
      !((settings.CenterOrder.length === 1 &&
         (settings.CenterOrder[0] === 0 || settings.CenterOrder[0] === 1)) ||
        (settings.CenterOrder.length === 2 && settings.CenterOrder[0] === 5 &&
         (settings.CenterOrder[1] === 0 || settings.CenterOrder[1] === 1)))) {
    throw new Error('Invalid seconds, step counter or center display setting');
  }
  return settings;
}

function readSettings() {
  var saved = localStorage.getItem(storageKey);
  if (saved !== null) {
    var settings = JSON.parse(saved);
    if (settings && settings.DisableSteps === undefined) settings.DisableSteps = false;
    if (settings && settings.CenterOrder === undefined) {
      settings.CenterOrder = [settings.CenterDisplay === undefined ? 0 : settings.CenterDisplay];
    }
    if (settings && Array.isArray(settings.CenterOrder) &&
        settings.CenterOrder.indexOf(5) < 0 &&
        settings.CenterOrder.every(function(value, index, order) {
          return typeof value === 'number' && value % 1 === 0 && value >= 0 && value <= 4 &&
            order.indexOf(value) === index;
        }) && settings.CenterOrder.length <= 5) {
      var preview = false, fallback = 1;
      settings.CenterOrder.some(function(value) {
        if (value === 0 || value === 1) { fallback = value; return true; }
        preview = true;
        return false;
      });
      settings.CenterOrder = preview ? [5, fallback] : [fallback];
    }
    return validateSettings(settings);
  }
  var legacy = localStorage.getItem('showSeconds');
  if (legacy !== null && legacy !== '0' && legacy !== '1') {
    throw new Error('Invalid saved seconds setting');
  }
  return { ShowSeconds: legacy === '0' ? 0 : 1, StepGoal: defaultGoal,
    DisableSteps: false, CenterOrder: [0] };
}

function sendSettings(settings) {
  enqueueMessage({ ShowSeconds: settings.ShowSeconds, StepGoal: settings.StepGoal,
    DisableSteps: settings.DisableSteps ? 1 : 0, CenterOrder: encodeOrder(settings.CenterOrder) }, true);
}

Pebble.addEventListener('ready', function() {
  try {
    var settings = readSettings();
    if (localStorage.getItem(storageKey) !== null || localStorage.getItem('showSeconds') !== null) {
      sendSettings(settings);
    }
    updateSources(settings.CenterOrder);
  } catch (error) {
    reportError('Could not read saved settings.', error);
  }
});

Pebble.addEventListener('appmessage', function(event) {
  if (previewEnabled && event.payload && event.payload.RequestWeather) {
    enqueueMessage({ SlotReset: 1 }, false);
    slotMessages.forEach(function(message, index) { if (message) sendSlot(index, message); });
  }
});

Pebble.addEventListener('showConfiguration', function() {
  try {
    var settings = readSettings();
    var options = ['Off (best battery life)', 'Always', '10 seconds after wrist shake / tap'];
    var secondsOptions = options.map(function(label, value) {
      return '<option value="' + value + '"' +
        (settings.ShowSeconds === value ? ' selected' : '') + '>' + label + '</option>';
    }).join('');
    var preview = settings.CenterOrder[0] === 5;
    var fallback = settings.CenterOrder[preview ? 1 : 0];
    var html = '<!doctype html><html><head><meta charset="utf-8">' +
      '<meta name="viewport" content="width=device-width,initial-scale=1">' +
      '<title>Roto360 settings</title><style>' +
      'body{font:18px system-ui,sans-serif;padding:24px;background:#111;color:#fff}' +
      'label{display:block;margin:24px 0}input,select{display:block;font:inherit;' +
      'max-width:100%;box-sizing:border-box;margin-top:8px;padding:8px}' +
      'button{font:inherit;padding:12px 24px;background:#05a;color:#fff;border:0}' +
      '</style></head><body><h1>Roto360</h1>' +
      '<form id="settings"><label>Seconds marker<select id="seconds">' + secondsOptions +
      '</select></label><p>Off and idle shake mode use minute ticks. Shake mode shows the blue ' +
      'inner tick for 10 seconds, then hides it. Motion detection varies by watch.</p>' +
      '<h2>Center display</h2><label><input id="preview" type="checkbox"' +
      (preview ? ' checked' : '') + '>Enable plugin complications (preview; off by default)</label>' +
      '<label>When plugins are off or unavailable<select id="fallback"><option value="0"' +
      (fallback === 0 ? ' selected' : '') + '>Battery percentage</option><option value="1"' +
      (fallback === 1 ? ' selected' : '') + '>Nothing</option></select></label>' +
      '<p>Choose and order complications in the Pebble app. The first available readable entry ' +
      'is shown; paused music and zero notifications are skipped. A notification entry shows a ' +
      'count. Weather uses Pebble temperature units. Music and notifications require Android.</p>' +
      '<label>Daily step goal<input id="goal" type="number" inputmode="numeric" min="1" max="' +
      maxGoal + '" step="1" required value="' + settings.StepGoal + '"></label>' +
      '<p>The green outer arc fills toward your goal, then stays full until the next day. ' +
      'Enable Pebble Health for steps. An amber dash means step data is unavailable. ' +
      'Steps stay on the watch; this goal does not change Pebble Health settings.</p>' +
      '<label><input id="disableSteps" type="checkbox"' +
      (settings.DisableSteps ? ' checked' : '') + '>Disable step counter (conserve battery)</label>' +
      '<p>Hides the step arc and unavailable-data dash, and stops this face from reading ' +
      'steps or listening for Health updates. Pebble Health still tracks activity. ' +
      'This does not change the seconds marker setting.</p>' +
      '<button type="submit">Save</button></form><script>' +
      'document.getElementById("settings").onsubmit=function(event){event.preventDefault();' +
      'var goal=Number(document.getElementById("goal").value);' +
      'if(!isFinite(goal)||goal%1!==0||goal<1||goal>' + maxGoal +
      '){alert("Enter a whole-number step goal from 1 to ' + maxGoal + '.");return;}' +
      'var order=document.getElementById("preview").checked?[5]:[];' +
      'order.push(Number(document.getElementById("fallback").value));' +
      'window.location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({' +
      'ShowSeconds:Number(document.getElementById("seconds").value),StepGoal:goal,' +
      'DisableSteps:document.getElementById("disableSteps").checked,' +
      'CenterOrder:order}));};' +
      '</script></body></html>';
    Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  } catch (error) {
    reportError('Could not open settings.', error);
  }
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event.response || event.response === 'CANCELLED') return;
  try {
    var response = event.response;
    if (response.charAt(0) !== '{') response = decodeURIComponent(response);
    var settings = JSON.parse(response);
    validateSettings(settings);
    var values = { ShowSeconds: settings.ShowSeconds, StepGoal: settings.StepGoal,
      DisableSteps: settings.DisableSteps, CenterOrder: settings.CenterOrder };
    localStorage.setItem(storageKey, JSON.stringify(values));
    sendSettings(values);
    updateSources(values.CenterOrder);
  } catch (error) {
    reportError('Could not save settings. Open settings and try again.', error);
  }
});
