// Pokémon World (web build): draws Unova's real 3D maps (converted from Pokémon Black by
// tools/pokemon_world/import_bw.py) with WebGL, under the game's own 2D picture.
//
// PW3D.setScene(zone, matrix, season): which BW zone (for its camera) and map matrix is on screen.
// The matrix (unova/matrices/m<id>.json) says which chunk file is in each cell; chunks near the
// camera are fetched (unova/chunks/<c>.bin + .json) with the season's textures and drawn.
// PW3D.draw(target, sprites) draws the map seen by BW's field camera looking at `target`
// (world units in the matrix: x east, y up, z south; a chunk is 512), then each sprite as a
// billboard standing at its world position, so they sit inside the scene (in front of / behind
// buildings) the way BW's do. A sprite is {x, y, z, w, h, image: RGBA Uint8Array of w*h pixels};
// its bottom centre is the point on the ground. 16 world units = one tile = 16 GBA pixels.
(function () {
  'use strict';
  var PW3D = window.PW3D = {};
  var gl, canvas, prog, spriteProg, spriteTex, quadBuf;
  var zones = null;      // unova/zones.json: per zone {matrix, camera, name}
  var scene = { zone: 0, matrix: -1, season: 0, cells: null, width: 0, height: 0 };
  var chunks = {};       // file name -> {state: 'loading' | 'ready', groups, vbuf, ibuf, info}
  var textures = {};     // url + wrap -> WebGL texture (or a promise of one)
  var images = {};       // url -> Promise<Image>
  var uintIndices = null;
  PW3D.base = 'unova/';
  PW3D.spriteTilt = 40;  // degrees the map sprites lean back (0 = upright; 40 keeps their GBA proportions)

  var VS = [
    'attribute vec3 aPos; attribute vec2 aUV; attribute vec4 aCol;',
    'uniform mat4 uMVP; uniform vec3 uOffset; varying vec2 vUV; varying vec4 vCol;',
    'void main() { gl_Position = uMVP * vec4(aPos * 0.125 + uOffset, 1.0); vUV = aUV; vCol = aCol; }'].join('\n');
  var FS = [
    'precision mediump float; varying vec2 vUV; varying vec4 vCol;',
    'uniform sampler2D uTex; uniform float uTextured; uniform float uAlpha; uniform float uAlphaTest;',
    'void main() {',
    '  vec4 t = uTextured > 0.5 ? texture2D(uTex, vUV) : vec4(1.0);',
    '  vec4 c = vec4(t.rgb * vCol.rgb, t.a * uAlpha);',
    '  if (c.a < uAlphaTest) discard;',
    '  gl_FragColor = c;',
    '}'].join('\n');
  var SVS = [
    'attribute vec2 aCorner; uniform mat4 uMVP; uniform vec3 uPos; uniform vec3 uRight; uniform vec3 uUp;',
    'uniform vec2 uSize; varying vec2 vUV;',
    'void main() { vec3 p = uPos + uRight * (aCorner.x - 0.5) * uSize.x + uUp * (1.0 - aCorner.y) * uSize.y;',
    '  gl_Position = uMVP * vec4(p, 1.0); vUV = aCorner; }'].join('\n');
  var FADE_VS = 'attribute vec2 aCorner; void main() { gl_Position = vec4(aCorner * 2.0 - 1.0, 0.0, 1.0); }';
  var FADE_FS = 'precision mediump float; uniform vec4 uColor; void main() { gl_FragColor = uColor; }';
  var fadeProg;
  var SFS = [
    'precision mediump float; varying vec2 vUV; uniform sampler2D uTex;',
    'void main() { vec4 c = texture2D(uTex, vUV); if (c.a < 0.5) discard; gl_FragColor = c; }'].join('\n');

  function compile(vs, fs) {
    function sh(type, src) {
      var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    }
    var p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    return p;
  }

  PW3D.init = function (c) {
    canvas = c;
    gl = canvas.getContext('webgl', { alpha: false, antialias: true, premultipliedAlpha: false });
    if (!gl) return false;
    prog = compile(VS, FS);
    spriteProg = compile(SVS, SFS);
    fadeProg = compile(FADE_VS, FADE_FS);
    quadBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, quadBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.STATIC_DRAW);
    spriteTex = gl.createTexture();
    uintIndices = gl.getExtension('OES_element_index_uint');
    fetch(PW3D.base + 'zones.json').then(function (r) { return r.json(); }).then(function (z) { zones = z; });
    return true;
  };

  function loadImage(url) {
    if (!images[url]) images[url] = new Promise(function (resolve) {
      var img = new Image();
      img.onload = function () { resolve(img); };
      img.onerror = function () { resolve(null); };
      img.src = url;
    });
    return images[url];
  }

  var WRAP;
  function texture(url, wrapS, wrapT) {
    var k = url + ':' + wrapS + ':' + wrapT;
    if (textures[k]) return textures[k];
    WRAP = WRAP || [gl.CLAMP_TO_EDGE, gl.REPEAT, gl.MIRRORED_REPEAT];
    textures[k] = loadImage(url).then(function (img) {
      if (!img) return null;
      var t = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, WRAP[wrapS]);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, WRAP[wrapT]);
      return (textures[k] = t);
    });
    return textures[k];
  }

  PW3D.setScene = function (zid, matrix, season) {
    scene.zone = zid;
    if (scene.matrix === matrix && scene.season === season) return;
    if (scene.season !== season) chunks = {};      // their textures differ by season
    scene.matrix = matrix; scene.season = season; scene.cells = null;
    fetch(PW3D.base + 'matrices/m' + matrix + '.json').then(function (r) { return r.json(); }).then(function (m) {
      if (scene.matrix !== matrix) return;
      scene.cells = m.cells; scene.width = m.width; scene.height = m.height;
    });
  };

  function loadChunk(name) {
    var c = chunks[name] = { state: 'loading' };
    var base = PW3D.base + 'chunks/' + name;
    var season = scene.season;
    Promise.all([fetch(base + '.json').then(function (r) { return r.json(); }),
                 fetch(base + '.bin').then(function (r) { return r.arrayBuffer(); })]).then(function (res) {
      var info = res[0], buf = res[1], dv = new DataView(buf);
      var ng = dv.getUint32(4, true), nv = dv.getUint32(8, true), ni = dv.getUint32(12, true);
      var groups = [];
      for (var g = 0; g < ng; g++) {
        var o = 16 + 16 * g;
        groups.push({ tex: dv.getUint16(o, true), wrapS: dv.getUint8(o + 2), wrapT: dv.getUint8(o + 3),
          alpha: dv.getUint8(o + 4) / 31, cull: dv.getUint8(o + 5), flags: dv.getUint8(o + 6),
          first: dv.getUint32(o + 8, true), count: dv.getUint32(o + 12, true), gltex: null });
      }
      var vo = 16 + 16 * ng, io = vo + 20 * nv, big = nv > 0xFFFF;
      if (big && !uintIndices) return;
      c.vbuf = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, c.vbuf);
      gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(buf, vo, 20 * nv), gl.STATIC_DRAW);
      c.ibuf = gl.createBuffer();
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, c.ibuf);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, big ? new Uint32Array(buf.slice(io, io + 4 * ni)) : new Uint16Array(buf.slice(io, io + 2 * ni)), gl.STATIC_DRAW);
      c.indexType = big ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
      c.indexSize = big ? 4 : 2;
      var set = info.mapTextures[season % info.mapTextures.length];
      groups.forEach(function (g) {
        if (g.tex === 0xFFFF) return;
        var t = info.textures[g.tex];
        var url = PW3D.base + 'tex/' + (t.kind === 'map' ? set : info.buildingTextures) + '/' + t.name + '.png';
        Promise.resolve(texture(url, g.wrapS, g.wrapT)).then(function (tex) { g.gltex = tex; });
      });
      c.groups = groups; c.info = info; c.state = 'ready';
    });
  }

  // the chunks around world (x, z) within `draw` cells: [{chunk, ox, oz}]; loads those within `load`
  function nearChunks(x, z, draw, load) {
    var out = [];
    if (!scene.cells) return out;
    var cx = Math.floor(x / 512), cz = Math.floor(z / 512);
    for (var j = -load; j <= load; j++) for (var i = -load; i <= load; i++) {
      var X = cx + i, Z = cz + j;
      if (X < 0 || Z < 0 || X >= scene.width || Z >= scene.height) continue;
      var name = scene.cells[Z * scene.width + X];
      if (!name) continue;
      if (!chunks[name]) loadChunk(name);
      if (Math.abs(i) <= draw && Math.abs(j) <= draw && chunks[name].state === 'ready')
        out.push({ chunk: chunks[name], ox: X * 512, oz: Z * 512 });
    }
    return out;
  }

  // --- matrices (column-major Float32Array(16), as WebGL wants) ---
  function perspective(fovy, aspect, near, far) {
    var f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
    return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0]);
  }
  function lookAt(eye, at, up) {
    var z = norm(sub(eye, at)), x = norm(cross(up, z)), y = cross(z, x);
    return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
      -dot(x, eye), -dot(y, eye), -dot(z, eye), 1]);
  }
  function mul(a, b) {
    var o = new Float32Array(16);
    for (var c = 0; c < 4; c++) for (var r = 0; r < 4; r++) {
      var s = 0; for (var k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s;
    }
    return o;
  }
  function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
  function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function norm(a) { var l = Math.sqrt(dot(a, a)) || 1; return [a[0] / l, a[1] / l, a[2] / l]; }

  PW3D.camera = null;     // the last camera: {eye, at, mvp}

  // BW's event camera. The field camera follows the player with the zone's pitch/yaw/distance; a
  // cutscene can move it to a fixed view (pitch, yaw, distance, target) and back, easing over n frames.
  var ev = { seq: -1, mode: 0, from: null, to: null, t: 0, n: 1, last: null };
  PW3D.eventCamera = function (mode, pitch, yaw, dist, x, y, z, frames, seq) {
    if (seq === ev.seq) return;
    ev.seq = seq;
    ev.mode = mode;
    ev.from = ev.last;
    ev.to = mode === 1 ? { pitch: pitch * 360 / 65536, yaw: yaw * 360 / 65536, distance: dist / 4096,
                           at: [x / 4096, y / 4096, z / 4096] } : null;
    ev.t = 0;
    ev.n = Math.max(1, frames);
  };
  function lerp(a, b, t) { return a + (b - a) * t; }
  function angLerp(a, b, t) { var d = ((b - a + 540) % 360) - 180; return a + d * t; }
  function viewOf(cam, target) {
    return { pitch: cam.pitch, yaw: cam.yaw, distance: cam.distance,
             at: [target[0] + cam.target[0], target[1] + cam.target[1], target[2] + cam.target[2]] };
  }
  // the view to draw this frame: the field camera, the event camera, or on the way between them
  function currentView(cam, target) {
    var field = viewOf(cam, target);
    if (ev.mode === 0 || (ev.mode === 2 && ev.t >= ev.n)) { ev.mode = 0; ev.last = field; return field; }
    var goal = ev.mode === 1 ? ev.to : field, from = ev.from || field;
    var t = Math.min(1, ev.t / ev.n);
    t = t * t * (3 - 2 * t);
    ev.t++;
    var v = { pitch: lerp(from.pitch, goal.pitch, t), yaw: angLerp(from.yaw, goal.yaw, t), distance: lerp(from.distance, goal.distance, t),
              at: [lerp(from.at[0], goal.at[0], t), lerp(from.at[1], goal.at[1], t), lerp(from.at[2], goal.at[2], t)] };
    ev.last = v;
    return v;
  }

  // fadeY 0..16 and fadeColor (RGB555): the game's palette fade, applied to the 3D picture too.
  PW3D.draw = function (target, sprites, fadeY, fadeColor) {
    if (!gl) return;
    var w = canvas.clientWidth * (window.devicePixelRatio || 1), h = canvas.clientHeight * (window.devicePixelRatio || 1);
    if (canvas.width !== Math.round(w) || canvas.height !== Math.round(h)) { canvas.width = Math.round(w); canvas.height = Math.round(h); }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    var cam = zones && zones[scene.zone] && zones[scene.zone].camera;
    if (!cam) return;
    var view = currentView(cam, target);
    var at = view.at;
    var pitch = view.pitch * Math.PI / 180, yaw = view.yaw * Math.PI / 180, d = view.distance;
    var eye = [at[0] + d * Math.cos(pitch) * Math.sin(yaw), at[1] + d * Math.sin(pitch), at[2] + d * Math.cos(pitch) * Math.cos(yaw)];
    var proj = perspective(cam.fov * Math.PI / 180, canvas.width / canvas.height, Math.max(cam.near, 1), cam.far);
    var mvp = mul(proj, lookAt(eye, at, [0, 1, 0]));
    PW3D.camera = { eye: eye, at: at, mvp: mvp };

    gl.useProgram(prog);
    gl.uniformMatrix4fv(gl.getUniformLocation(prog, 'uMVP'), false, mvp);
    var aPos = gl.getAttribLocation(prog, 'aPos'), aUV = gl.getAttribLocation(prog, 'aUV'), aCol = gl.getAttribLocation(prog, 'aCol');
    var uTextured = gl.getUniformLocation(prog, 'uTextured'), uAlpha = gl.getUniformLocation(prog, 'uAlpha'),
      uAlphaTest = gl.getUniformLocation(prog, 'uAlphaTest'), uOffset = gl.getUniformLocation(prog, 'uOffset');
    gl.enableVertexAttribArray(aPos); gl.enableVertexAttribArray(aUV); gl.enableVertexAttribArray(aCol);
    gl.enable(gl.DEPTH_TEST);
    gl.frontFace(gl.CCW);
    var near = nearChunks(target[0], target[2], 1, 2);
    // opaque and alpha-tested first, translucent after (without writing depth)
    [false, true].forEach(function (pass) {
      near.forEach(function (n) {
        var c = n.chunk;
        gl.bindBuffer(gl.ARRAY_BUFFER, c.vbuf);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, c.ibuf);
        gl.vertexAttribPointer(aPos, 3, gl.SHORT, false, 20, 0);
        gl.vertexAttribPointer(aUV, 2, gl.FLOAT, false, 20, 8);
        gl.vertexAttribPointer(aCol, 4, gl.UNSIGNED_BYTE, true, 20, 16);
        gl.uniform3fv(uOffset, [n.ox, 0, n.oz]);
        c.groups.forEach(function (g) {
          var translucent = !!(g.flags & 2);
          if (translucent !== pass || g.cull === 3) return;
          if (g.tex !== 0xFFFF && !(g.gltex instanceof WebGLTexture)) return;   // texture still loading
          if (g.cull === 0) gl.disable(gl.CULL_FACE);
          else { gl.enable(gl.CULL_FACE); gl.cullFace(g.cull === 1 ? gl.BACK : gl.FRONT); }
          if (g.gltex) { gl.bindTexture(gl.TEXTURE_2D, g.gltex); gl.uniform1f(uTextured, 1); } else gl.uniform1f(uTextured, 0);
          gl.uniform1f(uAlpha, g.alpha);
          gl.uniform1f(uAlphaTest, translucent ? 0.02 : 0.5);
          if (translucent) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); }
          gl.drawElements(gl.TRIANGLES, g.count, c.indexType, g.first * c.indexSize);
          gl.disable(gl.BLEND); gl.depthMask(true);
        });
      });
    });
    gl.disableVertexAttribArray(aPos); gl.disableVertexAttribArray(aUV); gl.disableVertexAttribArray(aCol);
    gl.disable(gl.CULL_FACE);

    if (sprites && sprites.length) drawSprites(sprites, eye, at, mvp);
    if (fadeY) {
      gl.disable(gl.DEPTH_TEST);
      gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.useProgram(fadeProg);
      gl.uniform4fv(gl.getUniformLocation(fadeProg, 'uColor'),
        [(fadeColor & 31) / 31, ((fadeColor >> 5) & 31) / 31, ((fadeColor >> 10) & 31) / 31, Math.min(fadeY, 16) / 16]);
      var a = gl.getAttribLocation(fadeProg, 'aCorner');
      gl.bindBuffer(gl.ARRAY_BUFFER, quadBuf);
      gl.enableVertexAttribArray(a); gl.vertexAttribPointer(a, 2, gl.FLOAT, false, 0, 0);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      gl.disableVertexAttribArray(a);
      gl.disable(gl.BLEND);
    }
  };

  function drawSprites(sprites, eye, at, mvp) {
    // billboards facing the camera horizontally, standing on their feet but leaning back (top away
    // from the camera) by PW3D.spriteTilt degrees, so the flat GBA sprites don't look like upright
    // cardboard cut-outs from BW's high camera (fully facing the camera would be the camera's pitch)
    var right = norm(cross([0, 1, 0], sub(eye, at)));
    var away = norm([at[0] - eye[0], 0, at[2] - eye[2]]), t = PW3D.spriteTilt * Math.PI / 180;
    var up = [away[0] * Math.sin(t), Math.cos(t), away[2] * Math.sin(t)];
    gl.useProgram(spriteProg);
    gl.uniformMatrix4fv(gl.getUniformLocation(spriteProg, 'uMVP'), false, mvp);
    gl.uniform3fv(gl.getUniformLocation(spriteProg, 'uRight'), right);
    gl.uniform3fv(gl.getUniformLocation(spriteProg, 'uUp'), up);
    var uPos = gl.getUniformLocation(spriteProg, 'uPos'), uSize = gl.getUniformLocation(spriteProg, 'uSize');
    var aCorner = gl.getAttribLocation(spriteProg, 'aCorner');
    gl.bindBuffer(gl.ARRAY_BUFFER, quadBuf);
    gl.enableVertexAttribArray(aCorner); gl.vertexAttribPointer(aCorner, 2, gl.FLOAT, false, 0, 0);
    gl.bindTexture(gl.TEXTURE_2D, spriteTex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    // the 3D map hides them where it's in front; among themselves they keep the GBA's order
    // (OAM slot: lower is in front), so draw the highest slot first and don't write depth
    gl.depthMask(false);
    sprites.slice().sort(function (a, b) { return b.order - a.order; }).forEach(function (s) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, s.w, s.h, 0, gl.RGBA, gl.UNSIGNED_BYTE, s.image);
      gl.uniform3fv(uPos, [s.x, s.y, s.z]);
      gl.uniform2fv(uSize, [s.w * (s.scale || 1), s.h * (s.scale || 1)]);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    });
    gl.disableVertexAttribArray(aCorner);
    gl.depthMask(true);
  }

  // Height of the ground at world (x, z): the converter samples the map model at each tile's centre.
  PW3D.groundHeight = function (x, z) {
    if (!scene.cells) return 0;
    var cx = Math.floor(x / 512), cz = Math.floor(z / 512);
    if (cx < 0 || cz < 0 || cx >= scene.width || cz >= scene.height) return 0;
    var c = chunks[scene.cells[cz * scene.width + cx]];
    if (!c || c.state !== 'ready') return 0;
    var tx = Math.max(0, Math.min(31, Math.floor((x - cx * 512) / 16))), tz = Math.max(0, Math.min(31, Math.floor((z - cz * 512) / 16)));
    return c.info.heights[tz * 32 + tx];
  };

  // --- BW's 3D cutscenes (Call3DDemo), converted by tools/pokemon_world/bw/demos.py ---
  // unova/demos/d<id>.json: frames, camera [pitch, yaw, roll (degrees), x, y, z] per frame, models;
  // a model's mesh (PWD1: f32 bone-local positions, a bone slot per vertex) and baked bone matrices
  // (3x4 rows per bone, every `step` frames). Each draw call has its own list of up to 60 bones.
  var DVS = [
    'attribute vec3 aPos; attribute float aBone; attribute vec2 aUV; attribute vec4 aCol;',
    'uniform mat4 uVP; uniform vec4 uBones[180]; uniform vec2 uUVOff; varying vec2 vUV; varying vec4 vCol;',
    'void main() { int i = int(aBone) * 3; vec4 p = vec4(aPos, 1.0);',
    '  vec3 w = vec3(dot(uBones[i], p), dot(uBones[i + 1], p), dot(uBones[i + 2], p));',
    '  gl_Position = uVP * vec4(w, 1.0); vUV = aUV + uUVOff; vCol = aCol; }'].join('\n');
  var demoProg = null, demos = {};
  PW3D.demoFov = 40;          // degrees
  PW3D.demoCamMode = 0;       // camera convention (see demoCamera)

  function loadDemo(id) {
    if (demos[id]) return demos[id];
    var d = demos[id] = { state: 'loading' };
    var base = PW3D.base + 'demos/';
    fetch(base + 'd' + id + '.json').then(function (r) { return r.json(); }).then(function (j) {
      d.info = j;
      return Promise.all(j.models.map(function (m) {
        return Promise.all([fetch(base + m.mesh).then(function (r) { return r.arrayBuffer(); }),
                            fetch(base + m.anim).then(function (r) { return r.arrayBuffer(); })]).then(function (bufs) {
          var dv = new DataView(bufs[0]), ng = dv.getUint32(4, true), nv = dv.getUint32(8, true), ni = dv.getUint32(12, true);
          var groups = [];
          for (var g = 0; g < ng; g++) {
            var o = 16 + 16 * g;
            groups.push({ tex: dv.getUint16(o, true), ws: dv.getUint8(o + 2), wt: dv.getUint8(o + 3), alpha: dv.getUint8(o + 4),
                          cull: dv.getUint8(o + 5), flags: dv.getUint8(o + 6), first: dv.getUint32(o + 8, true), count: dv.getUint32(o + 12, true) });
          }
          var vo = 16 + 16 * ng, io = vo + 28 * nv;
          var vbuf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vbuf);
          gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(bufs[0], vo, 28 * nv), gl.STATIC_DRAW);
          var big = nv > 65535, ibuf = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibuf);
          gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, big ? new Uint32Array(bufs[0].slice(io, io + 4 * ni)) : new Uint16Array(bufs[0].slice(io, io + 2 * ni)), gl.STATIC_DRAW);
          return { info: m, groups: groups, vbuf: vbuf, ibuf: ibuf, big: big, anim: new Float32Array(bufs[1]),
                   tex: m.textures.map(function (t) { return base + 'tex/' + t; }) };
        });
      }));
    }).then(function (models) { d.models = models; d.state = 'ready'; }).catch(function (e) { d.state = 'failed'; console.log('[demo] ' + e); });
    return d;
  }

  // BW's demo camera: pitch, yaw, roll in degrees, then the camera's position.
  // Orientation R = Ry(yaw) Rx(pitch) Rz(roll); the camera looks down its -z axis.
  function demoCamera(c) {
    var D = Math.PI / 180, px = c[0] * D, py = c[1] * D, pz = c[2] * D;
    var cx = Math.cos(px), sx = Math.sin(px), cy = Math.cos(py), sy = Math.sin(py), cz = Math.cos(pz), sz = Math.sin(pz);
    function m3(a, b) { var o = []; for (var r = 0; r < 3; r++) for (var k = 0; k < 3; k++) o[r * 3 + k] = a[r * 3] * b[k] + a[r * 3 + 1] * b[3 + k] + a[r * 3 + 2] * b[6 + k]; return o; }
    var RY = [cy, 0, sy, 0, 1, 0, -sy, 0, cy], RX = [1, 0, 0, 0, cx, -sx, 0, sx, cx], RZ = [cz, -sz, 0, sz, cz, 0, 0, 0, 1];
    var R = PW3D.demoCamMode === 1 ? m3(m3(RX, RY), RZ) : m3(m3(RY, RX), RZ);
    var e = [c[3], c[4], c[5]];
    return new Float32Array([R[0], R[1], R[2], 0, R[3], R[4], R[5], 0, R[6], R[7], R[8], 0,   // view = R^T (p - e)
      -(R[0] * e[0] + R[3] * e[1] + R[6] * e[2]), -(R[1] * e[0] + R[4] * e[1] + R[7] * e[2]), -(R[2] * e[0] + R[5] * e[1] + R[8] * e[2]), 1]);
  }

  PW3D.demoReady = function (id) { return loadDemo(id).state === 'ready'; };

  // draws cutscene `id` at `frame` (its own 30 fps frames); until it has loaded the screen is black
  PW3D.drawDemo = function (id, frame) {
    if (!gl) return;
    var w = canvas.clientWidth * (window.devicePixelRatio || 1), h = canvas.clientHeight * (window.devicePixelRatio || 1);
    if (canvas.width !== Math.round(w) || canvas.height !== Math.round(h)) { canvas.width = Math.round(w); canvas.height = Math.round(h); }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    var d = loadDemo(id);
    if (d.state !== 'ready') return;
    if (!demoProg) demoProg = compile(DVS, FS);
    var cams = d.info.camera, f = Math.max(0, Math.min(cams.length - 1, frame));
    var c0 = cams[Math.floor(f)], c1 = cams[Math.min(cams.length - 1, Math.floor(f) + 1)], t = f - Math.floor(f);
    var cam = c0.map(function (v, i) { return v + (c1[i] - v) * t; });
    var vp = mul(perspective(PW3D.demoFov * Math.PI / 180, canvas.width / canvas.height, 4, 40000), demoCamera(cam));
    gl.useProgram(demoProg);
    gl.uniformMatrix4fv(gl.getUniformLocation(demoProg, 'uVP'), false, vp);
    var aPos = gl.getAttribLocation(demoProg, 'aPos'), aBone = gl.getAttribLocation(demoProg, 'aBone'),
      aUV = gl.getAttribLocation(demoProg, 'aUV'), aCol = gl.getAttribLocation(demoProg, 'aCol');
    var uBones = gl.getUniformLocation(demoProg, 'uBones'), uUVOff = gl.getUniformLocation(demoProg, 'uUVOff'),
      uTextured = gl.getUniformLocation(demoProg, 'uTextured'), uAlpha = gl.getUniformLocation(demoProg, 'uAlpha'),
      uAlphaTest = gl.getUniformLocation(demoProg, 'uAlphaTest');
    [aPos, aBone, aUV, aCol].forEach(function (a) { if (a >= 0) gl.enableVertexAttribArray(a); });
    gl.enable(gl.DEPTH_TEST);
    var palette = new Float32Array(180 * 4);
    [false, true].forEach(function (pass) {
      var season = new Date().getMonth() % 4;     // like BW: January is spring
      d.models.forEach(function (m) {
        var info = m.info, nb = info.bones, step = info.step || 2;
        if (info.season !== undefined && info.season !== season) return;
        var nfr = m.anim.length / (12 * nb), fk = Math.min(nfr - 1, f / step), k0 = Math.floor(fk), k1 = Math.min(nfr - 1, k0 + 1), g = fk - k0;
        gl.bindBuffer(gl.ARRAY_BUFFER, m.vbuf);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, m.ibuf);
        gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, 28, 0);
        gl.vertexAttribPointer(aBone, 1, gl.UNSIGNED_SHORT, false, 28, 12);
        gl.vertexAttribPointer(aUV, 2, gl.FLOAT, false, 28, 16);
        gl.vertexAttribPointer(aCol, 4, gl.UNSIGNED_BYTE, true, 28, 24);
        m.groups.forEach(function (grp, gi) {
          var translucent = (grp.flags & 2) !== 0;
          if (translucent !== pass) return;
          var bones = info.draws[gi] || [];
          for (var b = 0; b < bones.length && b < 60; b++) {
            var a0 = (k0 * nb + bones[b]) * 12, a1 = (k1 * nb + bones[b]) * 12;
            for (var q = 0; q < 12; q++) palette[b * 12 + q] = m.anim[a0 + q] + (m.anim[a1 + q] - m.anim[a0 + q]) * g;
          }
          gl.uniform4fv(uBones, palette);
          var ti = grp.tex, pat = info.patterns[gi];
          if (pat) { for (var p = 0; p < pat.length; p++) if (pat[p][0] <= f) ti = pat[p][1]; }
          var sc = info.scroll[gi], uo = 0, vo = 0;
          if (sc) { if (sc[0].length) uo = sc[0][Math.floor(f) % sc[0].length]; if (sc[1].length) vo = sc[1][Math.floor(f) % sc[1].length]; }
          gl.uniform2f(uUVOff, uo, vo);
          var tex = ti !== 0xFFFF && m.tex[ti] ? texture(m.tex[ti], grp.ws, grp.wt) : null;
          if (tex && !(tex instanceof WebGLTexture)) return;     // still loading
          gl.uniform1f(uTextured, tex ? 1 : 0);
          if (tex) { gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex); }
          gl.uniform1f(uAlpha, grp.alpha / 31);
          gl.uniform1f(uAlphaTest, (grp.flags & 1) ? 0.5 : 0.0);
          if (grp.cull === 0) gl.disable(gl.CULL_FACE); else { gl.enable(gl.CULL_FACE); gl.cullFace(grp.cull === 1 ? gl.BACK : grp.cull === 2 ? gl.FRONT : gl.FRONT_AND_BACK); }
          if (translucent) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); }
          else { gl.disable(gl.BLEND); gl.depthMask(true); }
          gl.drawElements(gl.TRIANGLES, grp.count, m.big ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT, grp.first * (m.big ? 4 : 2));
        });
      });
    });
    gl.depthMask(true); gl.disable(gl.BLEND); gl.disable(gl.CULL_FACE);
    [aPos, aBone, aUV, aCol].forEach(function (a) { if (a >= 0) gl.disableVertexAttribArray(a); });
  };
})();
