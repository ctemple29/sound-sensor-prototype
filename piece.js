// piece.js: reads window.sense inside its own frame loop and answers.
//
// What is here now is a gauge: a colour per state and a circle that follows one
// signal. A gauge proves the sensing works and nothing more. Replace all of it
// with the piece your State Table describes. Read the object, never the camera.

const canvas = document.querySelector('#piece');
const ctx = canvas.getContext('2d');

function resize() {
  canvas.width = innerWidth * devicePixelRatio;
  canvas.height = innerHeight * devicePixelRatio;
}
addEventListener('resize', resize);
resize();

const COLORS = { nobody: '#0b0b0d', here: '#1d2a3a', open: '#3a1d2a' };

// A state with no colour above still gets a bright one of its own, picked from
// its name, so a new row in table.js is visible the moment it fires.
function colorFor(state) {
  if (COLORS[state]) return COLORS[state];
  let hash = 0;
  for (const ch of state) hash = (hash * 31 + ch.charCodeAt(0)) % 360;
  return `hsl(${hash}, 85%, 50%)`;
}

function frame() {
  const { state, signals, since } = window.sense;
  const held = performance.now() - since; // ms in this state

  ctx.fillStyle = colorFor(state);
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  if (state !== 'nobody') {
    const r = Math.min(canvas.width, canvas.height) * (0.05 + 0.35 * (signals.open || 0));
    const fadeIn = Math.min(1, held / 600);
    ctx.globalAlpha = fadeIn;
    ctx.fillStyle = '#f2efe9';
    ctx.beginPath();
    ctx.arc(canvas.width / 2, canvas.height / 2, r, 0, Math.PI * 2);
    ctx.fill();
    ctx.globalAlpha = 1;
  }

  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
