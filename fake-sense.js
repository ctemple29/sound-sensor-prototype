// fake-sense.js: the same object, with no sensor attached.
//
// Loaded first, so piece.js never reads an empty object. Until the camera is on:
//   number keys 1, 2, 3 ... set the state (in the order of your table's rows)
//   the mouse sets the signals: left to right is the first signal, top to bottom
//   the second
// When sense.js goes live it sets window.senseLive and this file stops writing.

(function () {
  const names = window.TABLE.states.map(row => row.name);
  const [first, second] = window.TABLE.signals;

  const signals = {};
  for (const name of window.TABLE.signals) signals[name] = 0;

  window.sense = {
    state: names[names.length - 1], // the last row, usually "nobody"
    signals,
    since: performance.now(),
  };

  addEventListener('keydown', e => {
    if (window.senseLive) return;
    const row = Number(e.key) - 1;
    if (row >= 0 && row < names.length && names[row] !== window.sense.state) {
      window.sense.state = names[row];
      window.sense.since = performance.now();
    }
  });

  addEventListener('pointermove', e => {
    if (window.senseLive) return;
    if (first) signals[first] = e.clientX / innerWidth;
    if (second) signals[second] = e.clientY / innerHeight;
  });
})();
