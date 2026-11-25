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
    var at = [target[0] + cam.target[0], target[1] + cam.target[1], target[2] + cam.target[2]];
    var pitch = cam.pitch * Math.PI / 180, yaw = cam.yaw * Math.PI / 180, d = cam.distance;
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
      gl.uniform2fv(uSize, [s.w, s.h]);
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
})();
