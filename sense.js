// sense.js: reads the camera, turns readings into signals, applies table.js,
// and writes window.sense.
//
// The instrument is the one from the Make a Face lab: MediaPipe Face Landmarker,
// 478 points and the same 52 named scores (press B to see them live). It is
// pinned to one version so it cannot change under you mid-module.
//
// What stays in the browser: every frame is read and dropped. Nothing here saves
// a frame or a landmark list. The model downloads once when the page loads;
// after that the Network panel should stay quiet.

// The library loads when you click Start, not before, so a blocked network shows
// up as a message on screen instead of a button that does nothing.
const LIBRARY = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@1.1.1/vision_bundle.mjs';
const WASM = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@1.1.1/wasm';
const MODEL = 'https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task';

const video = document.querySelector('#camera');
const button = document.querySelector('#start');
const readout = document.querySelector('#readout');
const scores = document.querySelector('#scores');

let landmarker = null;
let lastVideoTime = -1;
let latestScores = []; // the 52 named scores from the last frame, for the B panel only
let failed = false;    // the camera did not start; the readout holds the reason

// ---- Signals --------------------------------------------------------------
// Each function gets one frame's result and returns a number from 0 to 1.
// Add yours here. Its name has to match a name in table.js signals.

const SIGNALS = {
  // present: 1 when a face is in view, 0 when not. Smoothed below, so one
  // missed frame does not count as somebody leaving.
  present: result => (result.faceLandmarks.length > 0 ? 1 : 0),

  // open: the worked signal. jawOpen is one of the 52 scores, already 0 to 1.
  open: result => score(result, 'jawOpen'),

  // Your signal goes here. Two patterns to copy:
  //
  // A named score from the lab (the B panel shows every name):
  //   smile: result => (score(result, 'mouthSmileLeft') + score(result, 'mouthSmileRight')) / 2,
  //
  // Geometry from the 478 points (x and y run 0 to 1 across the frame):
  // A number that needs the last frame too, kept in a closure (how little the
  // nose moved since the previous frame):
  //   still: (() => {
  //     let last = null;
  //     return result => {
  //       const nose = result.faceLandmarks[0] && result.faceLandmarks[0][1];
  //       if (!nose) { last = null; return 0; }
  //       const moved = last ? Math.hypot(nose.x - last.x, nose.y - last.y) : 0;
  //       last = nose;
  //       return clamp(1 - moved * 40); // a jump of 1/40 of the frame reads 0
  //     };
  //   })(),
  //
  //   near: result => {
  //     const face = result.faceLandmarks[0];
  //     if (!face) return 0;
  //     const width = Math.abs(face[454].x - face[234].x); // cheek to cheek
  //     return clamp((width - 0.15) / 0.35);               // 0.15 far ... 0.5 at the screen
  //   },
};

// One of the 52 scores by name, or 0 when no face is in view.
function score(result, name) {
  const face = result.faceBlendshapes[0];
  if (!face) return 0;
  const found = face.categories.find(c => c.categoryName === name);
  return found ? found.score : 0;
}

function clamp(x) {
  return Math.min(1, Math.max(0, x));
}

// Smoothing: each new reading moves the signal a fraction of the way. Higher is
// jumpier, lower is calmer. Thresholds in table.js read the smoothed numbers.
const SMOOTHING = 0.25;

// ---- The State Table, applied ----------------------------------------------

const signals = window.sense.signals; // the object fake-sense.js made, kept
let candidate = null;
let candidateSince = 0;

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

// ---- The loop ---------------------------------------------------------------

function tick() {
  const now = performance.now();
  if (video.currentTime !== lastVideoTime) {
    lastVideoTime = video.currentTime;
    const result = landmarker.detectForVideo(video, now);
    for (const name of window.TABLE.signals) {
      const read = SIGNALS[name];
      if (!read) continue;
      signals[name] += (clamp(read(result)) - signals[name]) * SMOOTHING;
    }
    latestScores = result.faceBlendshapes[0] ? result.faceBlendshapes[0].categories : [];
  }
  applyTable(now);
  requestAnimationFrame(tick);
}

async function start() {
  button.disabled = true;
  button.textContent = 'Loading the model…';
  let FaceLandmarker, FilesetResolver;
  try {
    ({ FaceLandmarker, FilesetResolver } = await import(LIBRARY));
  } catch (err) {
    console.error(err);
    showFailure('The face library did not load (cdn.jsdelivr.net is blocked or offline). ' +
      'Press D: the fake sensor still works, so keep building the piece.');
    return;
  }
  try {
    const vision = await FilesetResolver.forVisionTasks(WASM);
    const options = {
      baseOptions: { modelAssetPath: MODEL, delegate: 'GPU' },
      runningMode: 'VIDEO',
      numFaces: 1,
      outputFaceBlendshapes: true,
    };
    try {
      landmarker = await FaceLandmarker.createFromOptions(vision, options);
    } catch {
      options.baseOptions.delegate = 'CPU'; // some machines refuse the GPU path
      landmarker = await FaceLandmarker.createFromOptions(vision, options);
    }
    button.textContent = 'Asking for the camera…';
    video.srcObject = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 480 } });
    await video.play();
    for (const name of window.TABLE.signals) {
      if (!SIGNALS[name]) console.warn(`table.js names a signal "${name}" that sense.js does not read yet.`);
    }
    window.senseLive = true;
    failed = false;
    button.remove();
    requestAnimationFrame(tick);
  } catch (err) {
    console.error(err);
    showFailure(`Camera did not start: ${err.message}`);
  }
}

function showFailure(message) {
  button.disabled = false;
  button.textContent = 'Did not start. Try again';
  failed = true;
  readout.hidden = false;
  readout.textContent = message;
}

button.addEventListener('click', start);

// ---- Readouts ---------------------------------------------------------------
// D shows the object piece.js reads. B shows all 52 scores, to find the name of
// the one you measured in the lab. V shows the camera. All off by default.

addEventListener('keydown', e => {
  const key = e.key.toLowerCase();
  if (key === 'd') {
    if (failed) { failed = false; readout.hidden = false; return; } // swap the message for the fake readout
    readout.hidden = !readout.hidden;
  }
  if (key === 'b') scores.hidden = !scores.hidden;
  if (key === 'v') video.classList.toggle('shown');
});

function drawReadouts() {
  if (!readout.hidden && !failed) {
    const { state, signals: s, since } = window.sense;
    const held = ((performance.now() - since) / 1000).toFixed(1);
    const lines = Object.entries(s).map(([k, v]) => `${k.padEnd(8)} ${v.toFixed(2)}`);
    readout.textContent =
      `${window.senseLive ? 'camera on' : 'fake: keys 1-9, mouse'}\n` +
      `state    ${state}  (${held} s)\n` + lines.join('\n');
  }
  if (!scores.hidden) {
    scores.textContent = latestScores.length
      ? latestScores.map(c => `${c.categoryName.padEnd(20)} ${c.score.toFixed(2)}`).join('\n')
      : 'no face in view (start the camera first)';
  }
  requestAnimationFrame(drawReadouts);
}
requestAnimationFrame(drawReadouts);
