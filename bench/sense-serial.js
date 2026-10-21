// bench/sense-serial.js: sense.js for bench pairs. Reads an Arduino over USB
// instead of the camera, turns readings into signals, applies the table, and
// writes the same window.sense. table.js, piece.js and fake-sense.js do not
// change. Bench pairs open bench.html, which loads this file and
// bench/table-bench.js.
//
// The Arduino prints lines of name and number pairs, e.g. "cm 42.5 light 612"
// (bench/bench.ino). Web Serial needs Chrome or Edge, and a page on localhost or
// https. Nothing is saved: each line is read and dropped.

const BAUD = 115200; // must match Serial.begin() in the sketch

const button = document.querySelector('#start');
const readout = document.querySelector('#readout');

// ---- Signals --------------------------------------------------------------
// Each function gets the latest reading, an object like { cm: 42.5 }, and
// returns a number from 0 to 1. Its name has to match a name in table.js signals.

const SIGNALS = {
  // near: 1 at the sensor, 0 at two metres or more.
  near: r => (r.cm === undefined ? 0 : clamp((200 - r.cm) / 190)),

  // present: someone within a metre and a half. Smoothed below, so one bad
  // echo does not count as somebody leaving.
  present: r => (r.cm !== undefined && r.cm < 150 ? 1 : 0),

  // Your signal goes here. A photoresistor printed as "light":
  //   shade: r => (r.light === undefined ? 0 : clamp(1 - r.light / 1023)),
};

function clamp(x) {
  return Math.min(1, Math.max(0, x));
}

const SMOOTHING = 0.25;

// ---- The State Table, applied (the same as sense.js) -------------------------

const signals = window.sense.signals;
let candidate = null;
let candidateSince = 0;
let latest = {};
let failed = false;

function applyTable(now) {
  const rows = window.TABLE.states;
  const winner = rows.find(row => row.when(signals));
  if (!winner || winner.name === window.sense.state) {
    candidate = null;
    return;
  }
  if (winner.name !== candidate) {
    candidate = winner.name;
    candidateSince = now;
  }
  if (now - candidateSince >= winner.hold * 1000) {
    window.sense.state = winner.name;
    window.sense.since = now;
    candidate = null;
  }
}

// ---- Reading the port --------------------------------------------------------

function parse(line) {
  const parts = line.trim().split(/\s+/);
  const reading = {};
  for (let i = 0; i + 1 < parts.length; i += 2) {
    const value = Number(parts[i + 1]);
    if (Number.isFinite(value)) reading[parts[i]] = value;
  }
  return reading;
}

function onReading(reading) {
  latest = reading;
  for (const name of window.TABLE.signals) {
    const read = SIGNALS[name];
    if (!read) continue;
    signals[name] += (clamp(read(reading)) - signals[name]) * SMOOTHING;
  }
}

async function start() {
  if (!('serial' in navigator)) {
    showFailure('This browser has no Web Serial. Open the page in Chrome, on localhost. Press D for the fake sensor.');
    return;
  }
  button.disabled = true;
  button.textContent = 'Pick the Arduino…';
  try {
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: BAUD });
    for (const name of window.TABLE.signals) {
      if (!SIGNALS[name]) console.warn(`table.js names a signal "${name}" that sense-serial.js does not read yet.`);
    }
    window.senseLive = true;
    failed = false;
    button.remove();
    requestAnimationFrame(tick);
    const reader = port.readable.pipeThrough(new TextDecoderStream()).getReader();
    let buffer = '';
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      buffer += value;
      const lines = buffer.split('\n');
      buffer = lines.pop(); // the last piece may be half a line
      for (const line of lines) onReading(parse(line));
    }
  } catch (err) {
    console.error(err);
    showFailure(`The Arduino did not connect: ${err.message}. Close the Arduino IDE's Serial Monitor (only one program can hold the port) and try again.`);
  }
}

function tick() {
  applyTable(performance.now());
  requestAnimationFrame(tick);
}

function showFailure(message) {
  button.disabled = false;
  button.textContent = 'Did not connect. Try again';
  failed = true;
  readout.hidden = false;
  readout.textContent = message;
}

button.textContent = 'Connect the Arduino';
button.addEventListener('click', start);

// ---- Readout ------------------------------------------------------------------
// D shows the object piece.js reads, and the raw line it came from.

addEventListener('keydown', e => {
  if (e.key.toLowerCase() !== 'd') return;
  if (failed) { failed = false; readout.hidden = false; return; }
  readout.hidden = !readout.hidden;
});

function drawReadout() {
  if (!readout.hidden && !failed) {
    const { state, signals: s, since } = window.sense;
    const held = ((performance.now() - since) / 1000).toFixed(1);
    const lines = Object.entries(s).map(([k, v]) => `${k.padEnd(8)} ${v.toFixed(2)}`);
    const raw = Object.entries(latest).map(([k, v]) => `${k} ${v}`).join('  ');
    readout.textContent =
      `${window.senseLive ? 'arduino: ' + raw : 'fake: keys 1-9, mouse'}\n` +
      `state    ${state}  (${held} s)\n` + lines.join('\n');
  }
  requestAnimationFrame(drawReadout);
}
requestAnimationFrame(drawReadout);
