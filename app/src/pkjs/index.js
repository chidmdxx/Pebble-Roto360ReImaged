'use strict';

var storageKey = 'settings';
var defaultGoal = 8000;
var maxGoal = 100000;

function reportError(message, error) {
  console.log(message + ': ' + String(error));
  Pebble.showSimpleNotificationOnPebble('Roto360 settings', message);
}

function validateSettings(settings) {
  if (!settings || typeof settings.ShowSeconds !== 'number' ||
      settings.ShowSeconds % 1 !== 0 || settings.ShowSeconds < 0 || settings.ShowSeconds > 2 ||
      typeof settings.StepGoal !== 'number' || settings.StepGoal % 1 !== 0 ||
      settings.StepGoal < 1 || settings.StepGoal > maxGoal) {
    throw new Error('Invalid seconds mode or daily step goal');
  }
  return settings;
}

function readSettings() {
  var saved = localStorage.getItem(storageKey);
  if (saved !== null) {
    return validateSettings(JSON.parse(saved));
  }
  var legacy = localStorage.getItem('showSeconds');
  if (legacy !== null && legacy !== '0' && legacy !== '1') {
    throw new Error('Invalid saved seconds setting');
  }
  return { ShowSeconds: legacy === '0' ? 0 : 1, StepGoal: defaultGoal };
}

function sendSettings(settings) {
  Pebble.sendAppMessage(settings, function() {}, function(error) {
    reportError('Could not update the watch. Reconnect and save settings again.', error);
  });
}

Pebble.addEventListener('ready', function() {
  try {
    if (localStorage.getItem(storageKey) !== null || localStorage.getItem('showSeconds') !== null) {
      sendSettings(readSettings());
    }
  } catch (error) {
    reportError('Could not read saved settings.', error);
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
      '<label>Daily step goal<input id="goal" type="number" inputmode="numeric" min="1" max="' +
      maxGoal + '" step="1" required value="' + settings.StepGoal + '"></label>' +
      '<p>The green outer arc fills toward your goal, then stays full until the next day. ' +
      'Enable Pebble Health for steps. An amber dash means step data is unavailable. ' +
      'Steps stay on the watch; this goal does not change Pebble Health settings.</p>' +
      '<button type="submit">Save</button></form><script>' +
      'document.getElementById("settings").onsubmit=function(event){event.preventDefault();' +
      'var goal=Number(document.getElementById("goal").value);' +
      'if(!isFinite(goal)||goal%1!==0||goal<1||goal>' + maxGoal +
      '){alert("Enter a whole-number step goal from 1 to ' + maxGoal + '.");return;}' +
      'window.location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({' +
      'ShowSeconds:Number(document.getElementById("seconds").value),StepGoal:goal}));};' +
      '</script></body></html>';
    Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  } catch (error) {
    reportError('Could not open settings.', error);
  }
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (!event.response || event.response === 'CANCELLED') {
    return;
  }
  try {
    // Current mobile apps decode once; older clients return the encoded fragment.
    var response = event.response;
    if (response.charAt(0) !== '{') {
      response = decodeURIComponent(response);
    }
    var settings = JSON.parse(response);
    validateSettings(settings);
    var values = { ShowSeconds: settings.ShowSeconds, StepGoal: settings.StepGoal };
    localStorage.setItem(storageKey, JSON.stringify(values));
    sendSettings(values);
  } catch (error) {
    reportError('Could not save settings. Open settings and try again.', error);
  }
});
