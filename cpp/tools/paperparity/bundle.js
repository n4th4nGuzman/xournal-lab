(() => {
  // ../../../app/src/renderer/paper.ts
  var MM = 72 / 25.4;
  var CM = MM * 10;
  var SUP = "\u2070\xB9\xB2\xB3\u2074\u2075\u2076\u2077\u2078\u2079";
  var sup = (n) => String(Math.abs(n)).split("").map((d) => SUP[Number(d)]).join("");
  var alpha = (ink, a) => `rgba(${ink.rgb},${a})`;
  function lines(ctx, color, width, build) {
    ctx.beginPath();
    build((x, y) => ctx.moveTo(x, y), (x, y) => ctx.lineTo(x, y));
    ctx.strokeStyle = color;
    ctx.lineWidth = width;
    ctx.stroke();
  }
  function text(ctx, s, x, y, ink, align = "center", size = 7.5) {
    ctx.font = `${size}px system-ui, sans-serif`;
    ctx.textAlign = align;
    ctx.textBaseline = "middle";
    ctx.fillStyle = ink.strong;
    ctx.fillText(s, x, y);
  }
  function arrow(ctx, x, y, dx, dy, ink) {
    const s = 5;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.lineTo(x - dx * s * 1.6 + dy * s * 0.6, y - dy * s * 1.6 - dx * s * 0.6);
    ctx.lineTo(x - dx * s * 1.6 - dy * s * 0.6, y - dy * s * 1.6 + dx * s * 0.6);
    ctx.closePath();
    ctx.fillStyle = ink.strong;
    ctx.fill();
  }
  var minus = (n) => (n < 0 ? "\u2212" : "") + Math.abs(+n.toFixed(2));
  var drawMm = (ctx, w, h, _p, ink) => {
    const nx = Math.floor(w / MM), ny = Math.floor(h / MM);
    const ox = (w - nx * MM) / 2, oy = (h - ny * MM) / 2;
    const tier = (i) => i % 10 === 0 ? 2 : i % 5 === 0 ? 1 : 0;
    const spec = [[0, 0.2, 0.22], [1, 0.35, 0.45], [2, 0.6, 0.8]];
    for (const [t, wd, a] of spec) {
      lines(ctx, alpha(ink, a), wd, (m, l) => {
        for (let i = 0; i <= nx; i++) if (tier(i) === t) {
          m(ox + i * MM, oy);
          l(ox + i * MM, oy + ny * MM);
        }
        for (let j = 0; j <= ny; j++) if (tier(j) === t) {
          m(ox, oy + j * MM);
          l(ox + nx * MM, oy + j * MM);
        }
      });
    }
  };
  var drawCartesian = (ctx, w, h, p, ink) => {
    const u = Number(p.unit) * CM;
    const sub = p.sub === "on";
    const labels = p.labels !== "off";
    const one = p.quad === "1";
    const x0 = one ? Math.round(44 / u) * u : Math.round(w / 2 / u) * u;
    const y0 = one ? Math.floor((h - 44) / u) * u : Math.round(h / 2 / u) * u;
    const left = one ? x0 : 0, right = w, top = 0, bottom = one ? y0 : h;
    const kx0 = Math.ceil((left - x0) / u - 1e-6), kx1 = Math.floor((right - x0) / u + 1e-6);
    const ky0 = Math.ceil((top - y0) / u - 1e-6), ky1 = Math.floor((bottom - y0) / u + 1e-6);
    if (sub) {
      const f = u / 5;
      lines(ctx, alpha(ink, 0.16), 0.3, (m, l) => {
        for (let i = Math.ceil((left - x0) / f); i <= Math.floor((right - x0) / f); i++) if (i % 5 !== 0) {
          m(x0 + i * f, top);
          l(x0 + i * f, bottom);
        }
        for (let j = Math.ceil((top - y0) / f); j <= Math.floor((bottom - y0) / f); j++) if (j % 5 !== 0) {
          m(left, y0 + j * f);
          l(right, y0 + j * f);
        }
      });
    }
    lines(ctx, alpha(ink, 0.42), 0.5, (m, l) => {
      for (let k = kx0; k <= kx1; k++) if (k !== 0) {
        m(x0 + k * u, top);
        l(x0 + k * u, bottom);
      }
      for (let k = ky0; k <= ky1; k++) if (k !== 0) {
        m(left, y0 + k * u);
        l(right, y0 + k * u);
      }
    });
    ctx.lineCap = "butt";
    lines(ctx, ink.strong, 1.2, (m, l) => {
      m(left, y0);
      l(right - 8, y0);
      m(x0, bottom);
      l(x0, top + 8);
    });
    arrow(ctx, right - 2, y0, 1, 0, ink);
    arrow(ctx, x0, top + 2, 0, -1, ink);
    text(ctx, "x", right - 10, y0 + 12, ink, "center", 9);
    text(ctx, "y", x0 + 11, top + 12, ink, "center", 9);
    if (labels) {
      const every = u < 0.8 * CM ? 2 : 1;
      for (let k = kx0; k <= kx1; k++) if (k !== 0 && k % every === 0 && x0 + k * u < right - 14) text(ctx, minus(k), x0 + k * u, y0 + 9, ink);
      for (let k = ky0; k <= ky1; k++) if (k !== 0 && k % every === 0 && y0 + k * u > top + 14) text(ctx, minus(-k), x0 - 8, y0 + k * u, ink, "right");
      text(ctx, "O", x0 - 7, y0 + 9, ink, "right");
    }
  };
  var drawTrig = (ctx, w, h, p, ink) => {
    const m = 26;
    const sx = (w - 2 * m) / (4 * Math.PI);
    const sy = 2 * sx;
    const x0 = w / 2, y0 = Math.round(h / 2 / (sy / 4)) * (sy / 4);
    const div = Number(p.grid);
    const step = Math.PI / div;
    const ymax = Math.floor((y0 - 20) / sy * 4) / 4, ymin = Math.floor((h - y0 - 20) / sy * 4) / 4;
    lines(ctx, alpha(ink, 0.16), 0.3, (mv, l) => {
      for (let i = -2 * div; i <= 2 * div; i++) if (i !== 0) {
        mv(x0 + i * step * sx, y0 - ymax * sy);
        l(x0 + i * step * sx, y0 + ymin * sy);
      }
      for (let j = -ymin * 4; j <= ymax * 4; j++) if (j !== 0 && j % 4 !== 0) {
        mv(m, y0 - j / 4 * sy);
        l(w - m, y0 - j / 4 * sy);
      }
    });
    lines(ctx, alpha(ink, 0.5), 0.5, (mv, l) => {
      for (let i = -4; i <= 4; i++) if (i !== 0) {
        mv(x0 + i * (Math.PI / 2) * sx, y0 - ymax * sy);
        l(x0 + i * (Math.PI / 2) * sx, y0 + ymin * sy);
      }
      for (let j = -Math.floor(ymin); j <= Math.floor(ymax); j++) if (j !== 0) {
        mv(m, y0 - j * sy);
        l(w - m, y0 - j * sy);
      }
    });
    lines(ctx, ink.strong, 1.2, (mv, l) => {
      mv(m - 6, y0);
      l(w - m, y0);
      mv(x0, y0 + ymin * sy);
      l(x0, y0 - ymax * sy - 4);
    });
    arrow(ctx, w - m + 6, y0, 1, 0, ink);
    arrow(ctx, x0, y0 - ymax * sy - 8, 0, -1, ink);
    const lab = ["", "\u03C0/2", "\u03C0", "3\u03C0/2", "2\u03C0"];
    for (let i = -4; i <= 4; i++) if (i !== 0) text(ctx, (i < 0 ? "\u2212" : "") + lab[Math.abs(i)], x0 + i * (Math.PI / 2) * sx, y0 + 9, ink);
    for (let j = -Math.floor(ymin); j <= Math.floor(ymax); j++) if (j !== 0) text(ctx, minus(j), x0 - 6, y0 - j * sy, ink, "right");
    text(ctx, "x", w - m + 4, y0 + 12, ink, "center", 9);
    text(ctx, "y", x0 + 10, y0 - ymax * sy - 10, ink, "center", 9);
  };
  var drawPolar = (ctx, w, h, p, ink) => {
    const cx = w / 2, cy = h / 2;
    const ring = Number(p.ring) * CM;
    const rmax = Math.floor((Math.min(w, h) / 2 - 26) / ring) * ring;
    const step = Number(p.step);
    const rings = Math.round(rmax / ring);
    lines(ctx, alpha(ink, 0.35), 0.45, (mv, l) => {
      for (let k = 1; k <= rings; k++) if (k % 5 !== 0) {
        mv(cx + k * ring, cy);
        for (let a = 1; a <= 120; a++) l(cx + Math.cos(a / 120 * 2 * Math.PI) * k * ring, cy - Math.sin(a / 120 * 2 * Math.PI) * k * ring);
      }
    });
    lines(ctx, alpha(ink, 0.7), 0.7, (mv, l) => {
      for (let k = 5; k <= rings; k += 5) {
        mv(cx + k * ring, cy);
        for (let a = 1; a <= 160; a++) l(cx + Math.cos(a / 160 * 2 * Math.PI) * k * ring, cy - Math.sin(a / 160 * 2 * Math.PI) * k * ring);
      }
      mv(cx + rmax, cy);
      for (let a = 1; a <= 160; a++) l(cx + Math.cos(a / 160 * 2 * Math.PI) * rmax, cy - Math.sin(a / 160 * 2 * Math.PI) * rmax);
    });
    for (const [major, wd, a] of [[false, 0.4, 0.3], [true, 0.7, 0.6]]) {
      lines(ctx, alpha(ink, a), wd, (mv, l) => {
        for (let d = 0; d < 360; d += step) if (d % 30 === 0 === major) {
          const r = d * Math.PI / 180;
          mv(cx, cy);
          l(cx + Math.cos(r) * rmax, cy - Math.sin(r) * rmax);
        }
      });
    }
    for (let d = 0; d < 360; d += 30) {
      const r = d * Math.PI / 180;
      text(ctx, `${d}\xB0`, cx + Math.cos(r) * (rmax + 14), cy - Math.sin(r) * (rmax + 14), ink);
    }
    for (let k = 1; k <= rings; k++) if (k % (ring < 0.8 * CM ? 2 : 1) === 0) text(ctx, +(k * Number(p.ring)).toFixed(2) + "", cx + k * ring + 1, cy + 7, ink, "left", 6.5);
  };
  var drawIso = (ctx, w, h, p, ink) => {
    const s = Number(p.size) * CM;
    const dx = s * Math.sqrt(3) / 2;
    const t = Math.tan(Math.PI / 6);
    const n = Math.floor(w / dx);
    const ox = (w - n * dx) / 2;
    ctx.save();
    ctx.translate(ox, 0);
    ctx.beginPath();
    ctx.rect(-ox, 0, w, h);
    ctx.clip();
    if (p.look === "dots") {
      ctx.fillStyle = alpha(ink, 0.6);
      for (let i = 0; i <= n; i++) for (let y = i % 2 ? s / 2 : 0; y <= h; y += s) ctx.fillRect(i * dx - 0.6, y - 0.6, 1.2, 1.2);
    } else {
      lines(ctx, alpha(ink, 0.4), 0.45, (mv, l) => {
        for (let i = 0; i <= n; i++) {
          mv(i * dx, 0);
          l(i * dx, h);
        }
        const span = n * dx;
        for (let j = Math.floor(-span * t / s) - 1; j <= Math.ceil(h / s) + 1; j++) {
          mv(0, j * s);
          l(span, j * s + span * t);
          mv(0, j * s);
          l(span, j * s - span * t);
        }
      });
    }
    ctx.restore();
  };
  var drawCornell = (ctx, w, h, _p, ink) => {
    const top = 74, cue = 150, sumY = h - 150;
    lines(ctx, alpha(ink, 0.3), 0.45, (m, l) => {
      for (let y = top + 26; y < sumY; y += 26) {
        m(cue, y);
        l(w - 24, y);
      }
      for (let y = sumY + 26; y < h - 30; y += 26) {
        m(24, y);
        l(w - 24, y);
      }
    });
    lines(ctx, alpha(ink, 0.8), 1, (m, l) => {
      m(24, top);
      l(w - 24, top);
      m(cue, top);
      l(cue, sumY);
      m(24, sumY);
      l(w - 24, sumY);
    });
    text(ctx, "Tema:", 26, 30, ink, "left", 9);
    text(ctx, "Fecha:", w - 130, 30, ink, "left", 9);
    lines(ctx, alpha(ink, 0.5), 0.5, (m, l) => {
      m(60, 38);
      l(w - 140, 38);
      m(w - 96, 38);
      l(w - 26, 38);
    });
    text(ctx, "Preguntas / ideas clave", 28, top + 10, ink, "left", 7.5);
    text(ctx, "Apuntes", cue + 6, top + 10, ink, "left", 7.5);
    text(ctx, "Resumen", 28, sumY + 10, ink, "left", 7.5);
  };
  function drawLog(ctx, w, h, p, ink) {
    const natural = p.base === "e";
    const both = p.axes === "xy";
    const cycles = Number(p.cycles);
    const L = 48, R = w - 26, T = 34, B = h - 42;
    const W = R - L, H = B - T;
    const xcycles = Math.max(1, Math.round(cycles * W / H));
    const inner = [];
    if (natural) for (let j = 1; j < 10; j++) inner.push([j / 10, j === 5 ? 1 : 0]);
    else for (let v = 2; v <= 9; v++) inner.push([Math.log10(v), v === 5 ? 1 : 0]);
    const label = (c) => natural ? c === 0 ? "1" : c === 1 ? "e" : `e${sup(c)}` : c <= 3 ? String(10 ** c) : `10${sup(c)}`;
    for (const [strength, wd, a] of [[0, 0.3, 0.3], [1, 0.5, 0.55]]) {
      lines(ctx, alpha(ink, a), wd, (m, l) => {
        for (let c = 0; c < cycles; c++) for (const [f, s] of inner) if (s === strength) {
          const y = B - (c + f) / cycles * H;
          m(L, y);
          l(R, y);
        }
        if (both) {
          for (let c = 0; c < xcycles; c++) for (const [f, s] of inner) if (s === strength) {
            const x = L + (c + f) / xcycles * W;
            m(x, T);
            l(x, B);
          } else for (let i = 1; i < 50; i++) if (i % 5 === 0 === (strength === 1)) {
            const x = L + i / 50 * W;
            m(x, T);
            l(x, B);
          }
        }
      });
    }
    lines(ctx, alpha(ink, 0.9), 0.9, (m, l) => {
      for (let c = 0; c <= cycles; c++) {
        const y = B - c / cycles * H;
        m(L, y);
        l(R, y);
      }
      if (both) for (let c = 0; c <= xcycles; c++) {
        const x = L + c / xcycles * W;
        m(x, T);
        l(x, B);
      }
      else for (let i = 0; i <= 10; i++) {
        const x = L + i / 10 * W;
        m(x, T);
        l(x, B);
      }
    });
    ctx.lineWidth = 1.2;
    ctx.strokeStyle = ink.strong;
    ctx.strokeRect(L, T, W, H);
    for (let c = 0; c <= cycles; c++) text(ctx, label(c), L - 5, B - c / cycles * H, ink, "right", 8);
    if (both) for (let c = 0; c <= xcycles; c++) text(ctx, label(c), L + c / xcycles * W, B + 10, ink, "center", 8);
    else for (let i = 0; i <= 10; i++) text(ctx, String(i), L + i / 10 * W, B + 10, ink, "center", 8);
    text(ctx, natural ? "escala logar\xEDtmica natural (ciclo = factor e)" : "escala logar\xEDtmica decimal (ciclo = d\xE9cada)", L, T - 12, ink, "left", 7.5);
  }
  var PAPER_KINDS = [
    { id: "mm", label: "Milimetrado", fallback: "graph", params: [], draw: drawMm },
    {
      id: "cart",
      label: "Plano cartesiano",
      fallback: "graph",
      params: [
        { key: "quad", label: "Ejes", options: [["4", "4 cuadrantes"], ["1", "1.er cuadrante"]] },
        { key: "unit", label: "Unidad", options: [["1", "1 cm"], ["0.5", "0,5 cm"], ["2", "2 cm"]] },
        { key: "sub", label: "Subdivisi\xF3n", options: [["off", "no"], ["on", "5 por unidad"]] },
        { key: "labels", label: "N\xFAmeros", options: [["on", "s\xED"], ["off", "no"]] }
      ],
      draw: drawCartesian
    },
    { id: "trig", label: "Trigonom\xE9trico (eje en \u03C0)", fallback: "graph", params: [{ key: "grid", label: "Rejilla", options: [["6", "cada \u03C0/6 (30\xB0)"], ["4", "cada \u03C0/4 (45\xB0)"]] }], draw: drawTrig },
    {
      id: "polar",
      label: "Polar",
      fallback: "plain",
      params: [
        { key: "step", label: "\xC1ngulo", options: [["15", "cada 15\xB0"], ["30", "cada 30\xB0"], ["10", "cada 10\xB0"]] },
        { key: "ring", label: "Anillos", options: [["1", "cada 1 cm"], ["0.5", "cada 0,5 cm"]] }
      ],
      draw: drawPolar
    },
    {
      id: "log",
      label: "Logar\xEDtmico",
      fallback: "graph",
      params: [
        { key: "base", label: "Base", options: [["10", "10 (decimal)"], ["e", "e (natural)"]] },
        { key: "axes", label: "Tipo", options: [["y", "Semilog (y log)"], ["xy", "Log-log"]] },
        { key: "cycles", label: "Ciclos", options: ["5", "1", "2", "3", "4", "6", "7"].map((c) => [c, c]) }
      ],
      draw: drawLog
    },
    {
      id: "iso",
      label: "Isom\xE9trico",
      fallback: "dotted",
      params: [
        { key: "look", label: "Aspecto", options: [["lines", "l\xEDneas"], ["dots", "puntos"]] },
        { key: "size", label: "Lado", options: [["1", "1 cm"], ["0.5", "0,5 cm"], ["2", "2 cm"]] }
      ],
      draw: drawIso
    },
    { id: "cornell", label: "Cornell (apuntes)", fallback: "ruled", params: [], draw: drawCornell }
  ];
  var paperKind = (id) => PAPER_KINDS.find((k) => k.id === id);
  function defaultSpec(kind) {
    const k = paperKind(kind);
    return k ? { kind, params: Object.fromEntries(k.params.map((p) => [p.key, p.options[0][0]])) } : void 0;
  }
  function parseSpec(raw) {
    if (!raw) return void 0;
    const [kind, ...rest] = raw.split(";");
    const spec = defaultSpec((kind ?? "").trim());
    if (!spec) return void 0;
    const k = paperKind(spec.kind);
    for (const kv of rest) {
      const [key, value] = kv.split("=");
      const param = k.params.find((p) => p.key === key?.trim());
      if (param && param.options.some((o) => o[0] === value?.trim())) spec.params[param.key] = value.trim();
    }
    return spec;
  }
  function formatSpec(s) {
    const k = paperKind(s.kind);
    return [s.kind, ...(k?.params ?? []).map((p) => `${p.key}=${s.params[p.key] ?? p.options[0][0]}`)].join(";");
  }
  function inkFor(bg) {
    const m = /rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/.exec(bg);
    const [r, g, b] = m ? [Number(m[1]), Number(m[2]), Number(m[3])] : [255, 255, 255];
    const lum = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
    return lum < 0.5 ? { rgb: "170,190,255", strong: "rgba(235,235,255,0.92)", dark: true } : { rgb: "60,100,160", strong: "rgba(25,25,40,0.9)", dark: false };
  }
  function drawPaper(ctx, w, h, bgCss, spec) {
    const k = paperKind(spec.kind);
    if (!k) return;
    ctx.save();
    ctx.setLineDash([]);
    ctx.lineCap = "butt";
    ctx.lineJoin = "round";
    k.draw(ctx, w, h, spec.params, inkFor(bgCss));
    ctx.restore();
  }

  // ../../../app/src/renderer/model.ts
  function texPose(t) {
    let rot = 0, flip = false;
    for (const a of t.attrs) {
      if (a[0] === "xlRot") {
        const v = Number(a[1]);
        if (Number.isFinite(v)) rot = v;
      } else if (a[0] === "xlFlip") flip = a[1] === "1";
    }
    return { rot, flip };
  }
  function attrValue(attrs, name) {
    return attrs.find((a) => a[0] === name)?.[1] ?? null;
  }
  function setAttr(attrs, name, value) {
    const a = attrs.find((x) => x[0] === name);
    if (a) a[1] = value;
    else attrs.push([name, value]);
  }
  function cssColor(raw) {
    const m = /^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/.exec(raw ?? "");
    if (!m) return "#000";
    const n = parseInt(m[1], 16);
    const a = m[2] ? parseInt(m[2], 16) / 255 : 1;
    return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a.toFixed(3)})`;
  }
  function nums(s) {
    const t = s.trim();
    if (!t) return [];
    const parts = t.split(/\s+/);
    const out = new Array(parts.length);
    for (let i = 0; i < parts.length; i++) out[i] = Number(parts[i]);
    return out;
  }
  function attrsOf(e) {
    return Array.from(e.attributes, (a) => [a.name, a.value]);
  }
  function stripWs(t) {
    return /\s/.test(t) ? t.replace(/\s+/g, "") : t;
  }
  function bboxOf(pts) {
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (let i = 0; i + 1 < pts.length; i += 2) {
      const x = pts[i], y = pts[i + 1];
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
    return [x0, y0, x1, y1];
  }
  var GLOW_MAX_SIZE = 40;
  function readGlow(attrs) {
    const size = Number(attrValue(attrs, "glow"));
    if (!(size > 0)) return void 0;
    const a = Number(attrValue(attrs, "glowAlpha") ?? "0.8");
    return { size: Math.min(GLOW_MAX_SIZE, size), intensity: Number.isFinite(a) ? Math.min(1, Math.max(0.05, a)) : 0.8 };
  }
  function readStroke(e) {
    const widths = nums(e.getAttribute("width") ?? "1.41");
    const pts = nums(e.textContent ?? "");
    if (pts.length & 1) pts.pop();
    const cap = e.getAttribute("capStyle");
    const attrs = attrsOf(e);
    const glow = readGlow(attrs);
    return {
      kind: "stroke",
      attrs,
      ...glow ? { glow } : {},
      tool: e.getAttribute("tool") ?? "pen",
      color: cssColor(e.getAttribute("color")),
      widths: widths.length ? widths : [1.41],
      pts,
      cap: cap === "butt" || cap === "square" ? cap : "round",
      dash: e.getAttribute("style"),
      bbox: bboxOf(pts)
    };
  }
  function readLayerChild(e) {
    switch (e.tagName) {
      case "stroke":
        return readStroke(e);
      case "text":
        return {
          kind: "text",
          attrs: attrsOf(e),
          text: e.textContent ?? "",
          font: e.getAttribute("font") ?? "Sans",
          size: Number(e.getAttribute("size") ?? 12),
          x: Number(e.getAttribute("x") ?? 0),
          y: Number(e.getAttribute("y") ?? 0),
          color: cssColor(e.getAttribute("color"))
        };
      case "image":
        return {
          kind: "image",
          attrs: attrsOf(e),
          left: Number(e.getAttribute("left")),
          top: Number(e.getAttribute("top")),
          right: Number(e.getAttribute("right")),
          bottom: Number(e.getAttribute("bottom")),
          b64: stripWs(e.textContent ?? "")
        };
      case "teximage":
        return {
          kind: "teximage",
          attrs: attrsOf(e),
          latex: e.getAttribute("text") ?? "",
          left: Number(e.getAttribute("left")),
          top: Number(e.getAttribute("top")),
          right: Number(e.getAttribute("right")),
          bottom: Number(e.getAttribute("bottom")),
          pdfB64: stripWs(e.textContent ?? "")
        };
      default:
        return { kind: "unsupported", tag: e.tagName, xml: new XMLSerializer().serializeToString(e) };
    }
  }
  function parseXopp(xml) {
    const dom = new DOMParser().parseFromString(xml, "application/xml");
    const err = dom.querySelector("parsererror");
    if (err) throw new Error("XML inv\xE1lido: " + (err.textContent ?? "").slice(0, 200));
    const root = dom.documentElement;
    if (root.tagName !== "xournal") throw new Error(`Ra\xEDz inesperada <${root.tagName}> (se esperaba <xournal>)`);
    let elementCount = 0;
    let dropped = 0;
    let title = null;
    let previewB64 = null;
    const pages = [];
    for (const p of Array.from(root.children)) {
      if (p.tagName === "title") {
        title = p.textContent ?? "";
        continue;
      }
      if (p.tagName === "preview") {
        previewB64 = stripWs(p.textContent ?? "");
        continue;
      }
      if (p.tagName !== "page") {
        dropped++;
        continue;
      }
      let bg = null;
      const layers = [];
      for (const c of Array.from(p.children)) {
        if (c.tagName === "background" && !bg) {
          const spec = parseSpec(c.getAttribute("xlPaper"));
          bg = {
            attrs: attrsOf(c),
            ...spec ? { paper: spec } : {},
            type: c.getAttribute("type") ?? "solid",
            color: cssColor(c.getAttribute("color") ?? "#ffffffff"),
            style: c.getAttribute("style") ?? "plain"
          };
        } else if (c.tagName === "layer") {
          const elements = Array.from(c.children).map(readLayerChild);
          elementCount += elements.length;
          layers.push({ attrs: attrsOf(c), elements });
        } else dropped++;
      }
      pages.push({
        attrs: attrsOf(p),
        width: Number(p.getAttribute("width") ?? 595.275591),
        height: Number(p.getAttribute("height") ?? 841.889764),
        background: bg ?? { attrs: [["type", "solid"], ["color", "#ffffffff"], ["style", "plain"]], type: "solid", color: "#fff", style: "plain" },
        layers
      });
    }
    return { rootAttrs: attrsOf(root), title, previewB64, pages, elementCount, droppedChildren: dropped };
  }

  // ../../../app/src/renderer/serialize.ts
  function fmt8(v) {
    if (!Number.isFinite(v)) return String(v);
    if (v === 0) return "0";
    const a = Math.abs(v);
    if (a >= 1e-4 && a < 1e8) return String(+v.toPrecision(8));
    const [mant, expS] = v.toExponential(7).split("e");
    const exp = Number(expS);
    const strip = (s) => s.includes(".") ? s.replace(/\.?0+$/, "") : s;
    if (exp < -4 || exp >= 8) {
      const sign = exp < 0 ? "-" : "+";
      return `${strip(mant)}e${sign}${String(Math.abs(exp)).padStart(2, "0")}`;
    }
    return strip(v.toFixed(7 - exp));
  }
  var escAttr = (s) => s.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "&#10;").replace(/\r/g, "&#13;");
  var escText = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  function attrString(attrs, override, fallback) {
    let out = "";
    const seen = /* @__PURE__ */ new Set();
    for (const [k, v] of attrs) {
      seen.add(k);
      out += ` ${k}="${escAttr(override && k in override ? override[k] : v)}"`;
    }
    if (override) {
      for (const k of Object.keys(override)) if (!seen.has(k)) {
        seen.add(k);
        out += ` ${k}="${escAttr(override[k])}"`;
      }
    }
    if (fallback) {
      for (const k of Object.keys(fallback)) if (!seen.has(k)) out += ` ${k}="${escAttr(fallback[k])}"`;
    }
    return out;
  }
  function hexAlpha(css) {
    const m = /^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*([\d.]+))?/.exec(css);
    if (!m) return "#000000ff";
    const h = (v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0");
    return `#${h(+m[1])}${h(+m[2])}${h(+m[3])}${h((m[4] === void 0 ? 1 : +m[4]) * 255)}`;
  }
  function ptsText(pts) {
    const parts = new Array(pts.length);
    for (let i = 0; i < pts.length; i++) parts[i] = fmt8(pts[i]);
    return parts.join(" ");
  }
  function stroke(s) {
    const width = s.widths.map(fmt8).join(" ");
    return `<stroke${attrString(s.attrs, { width })}>${ptsText(s.pts)}</stroke>
`;
  }
  function text2(t) {
    return `<text${attrString(t.attrs, { size: fmt8(t.size), x: fmt8(t.x), y: fmt8(t.y) }, { font: t.font, color: hexAlpha(t.color) })}>${escText(t.text)}</text>
`;
  }
  function image(i) {
    return `<image${attrString(i.attrs, { left: fmt8(i.left), top: fmt8(i.top), right: fmt8(i.right), bottom: fmt8(i.bottom) })}>${i.b64}</image>
`;
  }
  function texImage(t) {
    return `<teximage${attrString(t.attrs, { text: t.latex, left: fmt8(t.left), top: fmt8(t.top), right: fmt8(t.right), bottom: fmt8(t.bottom) })}>${t.pdfB64}</teximage>
`;
  }
  function element(e) {
    switch (e.kind) {
      case "stroke":
        return stroke(e);
      case "text":
        return text2(e);
      case "image":
        return image(e);
      case "teximage":
        return texImage(e);
      case "unsupported":
        return e.xml + "\n";
    }
  }
  function layer(l) {
    if (l.elements.length === 0) return `<layer${attrString(l.attrs)}/>
`;
    return `<layer${attrString(l.attrs)}>
${l.elements.map(element).join("")}</layer>
`;
  }
  function background(b) {
    return `<background${attrString(b.attrs)}/>
`;
  }
  function page(p) {
    const layers = p.layers.length ? p.layers : [{ attrs: [], elements: [] }];
    return `<page${attrString(p.attrs, { width: fmt8(p.width), height: fmt8(p.height) })}>
${background(p.background)}${layers.map(layer).join("")}</page>
`;
  }
  function serializeXopp(doc) {
    let out = `<?xml version="1.0" standalone="no"?>
<xournal${attrString(doc.rootAttrs)}>
`;
    if (doc.title !== null) out += `<title>${escText(doc.title)}</title>
`;
    if (doc.previewB64 !== null) out += `<preview>${doc.previewB64}</preview>
`;
    for (const p of doc.pages) out += page(p);
    return out + "</xournal>\n";
  }

  // ../../../app/src/renderer/markdown.ts
  var HEADING = [2, 1.6, 1.35, 1.15, 1.05, 1];
  var run = (text3, over = {}) => ({ text: text3, bold: false, italic: false, strike: false, underline: false, mono: false, link: false, ...over });
  function inlineRuns(src, base = {}) {
    const out = [];
    let buf = "";
    const st = { bold: !!base.bold, italic: false, strike: false };
    const flush = () => {
      if (buf) {
        out.push(run(buf, { ...base, ...st }));
        buf = "";
      }
    };
    const closes = (mark, from) => src.indexOf(mark, from) >= 0;
    const wordChar = (c) => !!c && /[\p{L}\p{N}]/u.test(c);
    let i = 0;
    while (i < src.length) {
      const c = src[i];
      if (c === "\\" && i + 1 < src.length && /[\\`*_~[\]()#>+\-.!]/.test(src[i + 1])) {
        buf += src[i + 1];
        i += 2;
        continue;
      }
      if (c === "`") {
        const end = src.indexOf("`", i + 1);
        if (end > i) {
          flush();
          out.push(run(src.slice(i + 1, end), { ...base, mono: true }));
          i = end + 1;
          continue;
        }
      }
      if (c === "[") {
        const m = /^\[([^\]]+)\]\(([^)\s]*)\)/.exec(src.slice(i));
        if (m) {
          flush();
          out.push(...inlineRuns(m[1], { ...base, ...st, underline: true, link: true }));
          i += m[0].length;
          continue;
        }
      }
      const two = src.slice(i, i + 2);
      if (two === "__" && !st.bold && wordChar(src[i - 1])) {
        buf += two;
        i += 2;
        continue;
      }
      if (two === "**" || two === "__") {
        const open = !st.bold;
        if (!open || closes(two, i + 2)) {
          flush();
          st.bold = open;
          i += 2;
          continue;
        }
      }
      if (two === "~~" && (st.strike || closes("~~", i + 2))) {
        flush();
        st.strike = !st.strike;
        i += 2;
        continue;
      }
      if ((c === "*" || c === "_") && two !== "**" && two !== "__") {
        const intraWord = c === "_" && wordChar(src[i - 1]) && wordChar(src[i + 1]);
        if (!intraWord && (st.italic || src[i + 1] !== " " && closes(c, i + 1))) {
          flush();
          st.italic = !st.italic;
          i += 1;
          continue;
        }
      }
      buf += c;
      i++;
    }
    flush();
    return out;
  }
  function parseMarkdown(src, size) {
    const lines2 = [];
    const raw = src.replace(/\r\n?/g, "\n").split("\n");
    let fence = false;
    const TAB = size * 1.3;
    for (let n = 0; n < raw.length; n++) {
      const line = raw[n];
      if (/^\s*```/.test(line)) {
        fence = !fence;
        continue;
      }
      if (fence) {
        lines2.push({ runs: [run(line, { mono: true })], size: size * 0.92, x: 0, quote: false, code: true, rule: false, gap: 0 });
        continue;
      }
      if (line.trim() === "") {
        lines2.push({ runs: [], size, x: 0, quote: false, code: false, rule: false, gap: -size * 0.55 });
        continue;
      }
      if (/^\s*([-*_])(\s*\1){2,}\s*$/.test(line)) {
        lines2.push({ runs: [], size, x: 0, quote: false, code: false, rule: true, gap: 0 });
        continue;
      }
      let m = /^(#{1,6})\s+(.*?)\s*#*\s*$/.exec(line);
      if (m) {
        const k = HEADING[m[1].length - 1];
        lines2.push({ runs: inlineRuns(m[2], { bold: true }), size: size * k, x: 0, quote: false, code: false, rule: false, gap: lines2.length ? size * 0.25 * k : 0 });
        continue;
      }
      m = /^(\s*)>\s?(.*)$/.exec(line);
      if (m) {
        lines2.push({ runs: inlineRuns(m[2], { italic: false }), size, x: TAB * 0.6, quote: true, code: false, rule: false, gap: 0 });
        continue;
      }
      m = /^(\s*)([-*+])\s+(?:\[([ xX])\]\s+)?(.*)$/.exec(line);
      if (m) {
        const level = Math.floor(m[1].replace(/\t/g, "  ").length / 2);
        const mark = m[3] !== void 0 ? m[3] === " " ? "\u2610" : "\u2611" : ["\u2022", "\u25E6", "\u25AA"][level % 3];
        lines2.push({ runs: inlineRuns(m[4]), size, x: TAB * (level + 1), marker: { text: mark, x: TAB * level + TAB * 0.2 }, quote: false, code: false, rule: false, gap: 0 });
        continue;
      }
      m = /^(\s*)(\d+)[.)]\s+(.*)$/.exec(line);
      if (m) {
        const level = Math.floor(m[1].replace(/\t/g, "  ").length / 2);
        lines2.push({ runs: inlineRuns(m[3]), size, x: TAB * (level + 1), marker: { text: `${m[2]}.`, x: TAB * level + TAB * 0.1 }, quote: false, code: false, rule: false, gap: 0 });
        continue;
      }
      lines2.push({ runs: inlineRuns(line.trimEnd()), size, x: 0, quote: false, code: false, rule: false, gap: 0 });
    }
    return lines2;
  }
  var isMarkdown = (t) => t.attrs.some((a) => a[0] === "xlMd" && a[1] === "1");
  var FAMILY = (font) => font === "Sans" ? "system-ui, sans-serif" : `"${font}", sans-serif`;
  var MONO = "Consolas, 'Cascadia Mono', ui-monospace, monospace";
  var runFont = (r, size, font) => `${r.italic ? "italic " : ""}${r.bold ? "bold " : ""}${size}px ${r.mono ? MONO : FAMILY(font)}`;
  var LINE = 1.2;
  var measureCtx = null;
  var cache = /* @__PURE__ */ new WeakMap();
  function layoutMarkdown(owner) {
    const key = `${owner.size}|${owner.font}|${owner.text}`;
    const hit = cache.get(owner);
    if (hit && hit.key === key) return hit.layout;
    measureCtx ??= document.createElement("canvas").getContext("2d");
    const ctx = measureCtx;
    const out = [];
    let y = 0, width = 0;
    for (const line of parseMarkdown(owner.text, owner.size)) {
      const h = line.size * LINE;
      y += Math.max(0, line.gap);
      const widths = line.runs.map((r) => {
        ctx.font = runFont(r, line.size, owner.font);
        return ctx.measureText(r.text).width;
      });
      const w = widths.reduce((a, b) => a + b, 0);
      const lineH = line.gap < 0 ? h + line.gap : h;
      out.push({ line, y, h: lineH, widths, width: w });
      width = Math.max(width, line.x + w + (line.code ? owner.size * 0.4 : 0));
      y += lineH;
    }
    const layout = { lines: out, width: Math.max(width, owner.size), height: Math.max(y, owner.size * LINE) };
    cache.set(owner, { key, layout });
    return layout;
  }
  function drawMarkdown(ctx, owner) {
    const lay = layoutMarkdown(owner);
    ctx.textBaseline = "alphabetic";
    for (const l of lay.lines) {
      const { line } = l;
      const top = owner.y + l.y;
      const base = top + l.h * 0.5 + line.size * 0.32;
      if (line.code) {
        ctx.fillStyle = "rgba(128,128,128,0.18)";
        ctx.fillRect(owner.x - owner.size * 0.15, top, lay.width + owner.size * 0.3, l.h);
      }
      if (line.rule) {
        ctx.strokeStyle = owner.color;
        ctx.globalAlpha = 0.6;
        ctx.lineWidth = Math.max(0.6, owner.size * 0.06);
        ctx.beginPath();
        ctx.moveTo(owner.x, top + l.h / 2);
        ctx.lineTo(owner.x + Math.max(lay.width, owner.size * 6), top + l.h / 2);
        ctx.stroke();
        ctx.globalAlpha = 1;
        continue;
      }
      if (line.quote) {
        ctx.fillStyle = owner.color;
        ctx.globalAlpha = 0.45;
        ctx.fillRect(owner.x + line.x - owner.size * 0.7, top, owner.size * 0.18, l.h);
        ctx.globalAlpha = 1;
      }
      if (line.marker) {
        ctx.font = `${line.size}px ${FAMILY(owner.font)}`;
        ctx.fillStyle = owner.color;
        ctx.fillText(line.marker.text, owner.x + line.marker.x, base);
      }
      let x = owner.x + line.x;
      line.runs.forEach((r, i) => {
        const w = l.widths[i];
        ctx.font = runFont(r, line.size, owner.font);
        ctx.fillStyle = r.link ? "#2f6fd0" : owner.color;
        if (line.quote) ctx.globalAlpha = 0.8;
        ctx.fillText(r.text, x, base);
        ctx.globalAlpha = 1;
        if (r.strike || r.underline) {
          ctx.strokeStyle = ctx.fillStyle;
          ctx.lineWidth = Math.max(0.5, line.size * 0.06);
          ctx.beginPath();
          const yy = r.strike ? base - line.size * 0.3 : base + line.size * 0.12;
          ctx.moveTo(x, yy);
          ctx.lineTo(x + w, yy);
          ctx.stroke();
        }
        x += w;
      });
    }
  }
  function markdownSize(owner) {
    const l = layoutMarkdown(owner);
    return [l.width, l.height];
  }

  // ../../../app/src/renderer/geometry.ts
  var LINE_HEIGHT = 1.2;
  var measureCtx2 = null;
  function textBBox(t) {
    if (isMarkdown(t)) {
      const [w2, h] = markdownSize(t);
      return [t.x, t.y, t.x + w2, t.y + h];
    }
    measureCtx2 ??= document.createElement("canvas").getContext("2d");
    const family = t.font === "Sans" ? "system-ui, sans-serif" : `"${t.font}", sans-serif`;
    measureCtx2.font = `${t.size}px ${family}`;
    const lines2 = t.text.split("\n");
    let w = 0;
    for (const l of lines2) w = Math.max(w, measureCtx2.measureText(l).width);
    return [t.x, t.y, t.x + Math.max(w, t.size), t.y + lines2.length * t.size * LINE_HEIGHT];
  }
  function maxOf(a) {
    let m = -Infinity;
    for (let i = 0; i < a.length; i++) if (a[i] > m) m = a[i];
    return m;
  }
  function texBounds(el) {
    const { rot } = texPose(el);
    if (rot === 0) return [el.left, el.top, el.right, el.bottom];
    const cx = (el.left + el.right) / 2, cy = (el.top + el.bottom) / 2, hw = (el.right - el.left) / 2, hh = (el.bottom - el.top) / 2;
    const c = Math.abs(Math.cos(rot)), s = Math.abs(Math.sin(rot));
    const w = hw * c + hh * s, h = hw * s + hh * c;
    return [cx - w, cy - h, cx + w, cy + h];
  }
  function elementBBox(el) {
    switch (el.kind) {
      case "stroke": {
        const h = maxOf(el.widths) / 2;
        return [el.bbox[0] - h, el.bbox[1] - h, el.bbox[2] + h, el.bbox[3] + h];
      }
      case "text":
        return textBBox(el);
      case "image":
        return [el.left, el.top, el.right, el.bottom];
      case "teximage":
        return texBounds(el);
      case "unsupported":
        return null;
    }
  }
  var SNAP15 = Math.PI / 12;
  function localQuadratic(p, s, sigma) {
    const n = p.length >> 1;
    const R = 3 * sigma;
    const inv2 = 1 / 2;
    const out = new Array(2 * n);
    let lo = 0, hi = 0;
    for (let i = 0; i < n; i++) {
      const si = s[i];
      while (s[lo] < si - R) lo++;
      while (hi < n - 1 && s[hi + 1] <= si + R) hi++;
      let S0 = 0, S1 = 0, S2 = 0, S3 = 0, S4 = 0, X0 = 0, X1 = 0, X2 = 0, Y0 = 0, Y1 = 0, Y2 = 0;
      for (let j = lo; j <= hi; j++) {
        const u = (s[j] - si) / sigma;
        const ds = ((j < n - 1 ? s[j + 1] : s[j]) - (j > 0 ? s[j - 1] : s[j])) / 2;
        const w = Math.exp(-u * u * inv2) * ds;
        const u2 = u * u;
        const x = p[2 * j], y = p[2 * j + 1];
        S0 += w;
        S1 += w * u;
        S2 += w * u2;
        S3 += w * u2 * u;
        S4 += w * u2 * u2;
        X0 += w * x;
        X1 += w * u * x;
        X2 += w * u2 * x;
        Y0 += w * y;
        Y1 += w * u * y;
        Y2 += w * u2 * y;
      }
      const c0 = S2 * S4 - S3 * S3;
      const det = S0 * c0 - S1 * (S1 * S4 - S2 * S3) + S2 * (S1 * S3 - S2 * S2);
      if (S0 > 0 && Math.abs(det) > 1e-9 * S0 * S0 * S0) {
        out[2 * i] = (X0 * c0 - S1 * (X1 * S4 - S3 * X2) + S2 * (X1 * S3 - S2 * X2)) / det;
        out[2 * i + 1] = (Y0 * c0 - S1 * (Y1 * S4 - S3 * Y2) + S2 * (Y1 * S3 - S2 * Y2)) / det;
      } else {
        out[2 * i] = S0 > 0 ? X0 / S0 : p[2 * i];
        out[2 * i + 1] = S0 > 0 ? Y0 / S0 : p[2 * i + 1];
      }
    }
    return out;
  }
  function smoothFreehand(p, step) {
    const n = p.length >> 1;
    if (n < 4 || step <= 0) return p.slice();
    const s = new Float64Array(n);
    for (let i = 1; i < n; i++) s[i] = s[i - 1] + Math.hypot(p[2 * i] - p[2 * i - 2], p[2 * i + 1] - p[2 * i - 1]);
    const sigma = 8 * step;
    if (s[n - 1] < 2 * sigma) return p.slice();
    const half = step / 2;
    let cur = p.slice();
    for (let iter = 0; iter < 3; iter++) {
      cur = localQuadratic(cur, s, sigma);
      for (let k = 0; k < cur.length; k++) cur[k] = Math.min(p[k] + half, Math.max(p[k] - half, cur[k]));
      cur[0] = p[0];
      cur[1] = p[1];
      cur[2 * n - 2] = p[2 * n - 2];
      cur[2 * n - 1] = p[2 * n - 1];
    }
    return cur;
  }
  function fgcd(a, b, tol) {
    let x = Math.max(a, b), y = Math.min(a, b);
    while (y > tol) {
      let r = x % y;
      if (r > y - tol) r = 0;
      x = y;
      y = r;
    }
    return x;
  }
  function quantizationStep(p) {
    const n = p.length >> 1;
    if (n < 13) return 0;
    const ds = [];
    for (let i = 1; i < n; i++) {
      for (let a = 0; a < 2; a++) {
        const d = Math.abs(p[2 * i + a] - p[2 * i - 2 + a]);
        if (d > 1e-3) ds.push(d);
      }
    }
    if (ds.length < 12) return 0;
    let g = ds[0];
    for (const d of ds) {
      g = fgcd(g, d, 2e-3);
      if (g < 0.05) return 0;
    }
    return g > 12 ? 0 : g;
  }
  var STAIR_PX = 1.5;
  function displayPoints(s, scale) {
    let v = s.view;
    if (!v || v.src !== s.pts) {
      const exact = s.attrs.some((a) => a[0] === "xlPlane" || a[0] === "xlRiemann");
      v = { src: s.pts, step: exact ? 0 : quantizationStep(s.pts), smooth: null };
      s.view = v;
    }
    if (v.step * scale < STAIR_PX) return s.pts;
    return v.smooth ??= smoothFreehand(s.pts, v.step);
  }

  // ../../../app/src/renderer/pdfraster.ts
  var lib = null;
  function load() {
    lib ??= (async () => {
      const url = new URL("pdf.mjs", location.href).href;
      const m = await import(
        /* @vite-ignore */
        url
      );
      m.GlobalWorkerOptions.workerSrc = new URL("pdf.worker.mjs", location.href).href;
      return m;
    })();
    return lib;
  }
  var toBytes = (b64) => Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  async function rasterizePdf(b64, widthPx, pageNo = 1, background2) {
    const pdfjs = await load();
    const task = pdfjs.getDocument({ data: toBytes(b64) });
    const pdf = await task.promise;
    try {
      const page2 = await pdf.getPage(pageNo);
      const viewport = page2.getViewport({ scale: widthPx / page2.getViewport({ scale: 1 }).width });
      const canvas = document.createElement("canvas");
      canvas.width = Math.max(1, Math.ceil(viewport.width));
      canvas.height = Math.max(1, Math.ceil(viewport.height));
      await page2.render(background2 === void 0 ? { canvas, viewport } : { canvas, viewport, background: background2 }).promise;
      return canvas;
    } finally {
      await task.destroy();
    }
  }
  function unblend(cv, ink, onBlack) {
    const ctx = cv.getContext("2d");
    const img = ctx.getImageData(0, 0, cv.width, cv.height);
    const d = img.data;
    const dist = ink.map((v) => onBlack ? v : 255 - v);
    const ch = dist.indexOf(Math.max(...dist));
    const den = Math.max(1, dist[ch]);
    for (let i = 0; i < d.length; i += 4) {
      const p = d[i + ch];
      const a = Math.min(1, Math.max(0, (onBlack ? p : 255 - p) / den));
      const al = Math.round(a * 255);
      d[i] = al ? ink[0] : 0;
      d[i + 1] = al ? ink[1] : 0;
      d[i + 2] = al ? ink[2] : 0;
      d[i + 3] = al;
    }
    ctx.putImageData(img, 0, 0);
    return cv;
  }
  async function rasterizeAuto(b64, widthPx) {
    const cv = await rasterizePdf(b64, widthPx);
    const d = cv.getContext("2d").getImageData(0, 0, cv.width, cv.height).data;
    let maxD = 0;
    for (let i = 0; i < d.length; i += 4) maxD = Math.max(maxD, 765 - d[i] - d[i + 1] - d[i + 2]);
    if (maxD < 30) {
      const dark = await rasterizePdf(b64, widthPx, 1, "rgb(0,0,0)");
      const dd = dark.getContext("2d").getImageData(0, 0, dark.width, dark.height).data;
      let best = 0;
      for (let i = 0; i < dd.length; i += 4) best = Math.max(best, dd[i] + dd[i + 1] + dd[i + 2]);
      if (best < 30) return document.createElement("canvas");
      const sum2 = [0, 0, 0];
      let n2 = 0;
      for (let i = 0; i < dd.length; i += 4) if (dd[i] + dd[i + 1] + dd[i + 2] >= 0.9 * best) {
        sum2[0] += dd[i];
        sum2[1] += dd[i + 1];
        sum2[2] += dd[i + 2];
        n2++;
      }
      return unblend(dark, [Math.round(sum2[0] / n2), Math.round(sum2[1] / n2), Math.round(sum2[2] / n2)], true);
    }
    const sum = [0, 0, 0];
    let n = 0;
    for (let i = 0; i < d.length; i += 4) {
      if (765 - d[i] - d[i + 1] - d[i + 2] >= 0.9 * maxD) {
        sum[0] += d[i];
        sum[1] += d[i + 1];
        sum[2] += d[i + 2];
        n++;
      }
    }
    return unblend(cv, [Math.round(sum[0] / n), Math.round(sum[1] / n), Math.round(sum[2] / n)], false);
  }

  // ../../../app/src/renderer/render.ts
  var LINE2 = "rgba(150,150,150,1)";
  var GRID = 14.17;
  function drawBackground(ctx, page2) {
    const { width: w, height: h, background: bg } = page2;
    ctx.fillStyle = bg.color;
    ctx.fillRect(0, 0, w, h);
    if (bg.paper) {
      drawPaper(ctx, w, h, bg.color, bg.paper);
      return;
    }
    ctx.strokeStyle = LINE2;
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    switch (bg.style) {
      case "lined":
      case "ruled": {
        for (let y = 80; y < h; y += 24) {
          ctx.moveTo(0, y);
          ctx.lineTo(w, y);
        }
        ctx.stroke();
        if (bg.style === "lined") {
          ctx.strokeStyle = "rgba(220,80,80,1)";
          ctx.beginPath();
          ctx.moveTo(72, 0);
          ctx.lineTo(72, h);
          ctx.stroke();
        }
        break;
      }
      case "graph": {
        for (let x = 0; x <= w; x += GRID) {
          ctx.moveTo(x, 0);
          ctx.lineTo(x, h);
        }
        for (let y = 0; y <= h; y += GRID) {
          ctx.moveTo(0, y);
          ctx.lineTo(w, y);
        }
        ctx.stroke();
        break;
      }
      case "dotted": {
        ctx.fillStyle = LINE2;
        for (let y = GRID; y < h; y += GRID) for (let x = GRID; x < w; x += GRID) ctx.fillRect(x - 0.5, y - 0.5, 1, 1);
        break;
      }
      default:
        break;
    }
  }
  var DASHES = { dash: [6, 3], dashdot: [6, 3, 0.5, 3], dot: [0.5, 3] };
  function withAlpha(css, a) {
    const m = /^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)/.exec(css);
    if (m) return `rgba(${m[1]},${m[2]},${m[3]},${a})`;
    const h = /^#([0-9a-f]+)$/i.exec(css);
    if (!h || ![3, 6, 8].includes(h[1].length)) return css;
    const x = h[1].length === 3 ? [...h[1]].map((c) => c + c).join("") : h[1];
    return `rgba(${parseInt(x.slice(0, 2), 16)},${parseInt(x.slice(2, 4), 16)},${parseInt(x.slice(4, 6), 16)},${a})`;
  }
  function glowFilter(color, g, scale) {
    const sigma = Math.max(0.3, g.size * scale * 0.5);
    const c = withAlpha(color, g.intensity);
    return [1, 0.45, 0.16].map((k) => `drop-shadow(0 0 ${(sigma * k).toFixed(2)}px ${c})`).join(" ");
  }
  var MAX_GLOW_PIXELS = 24e6;
  function drawStroke(ctx, s, lite = false) {
    if (s.tool === "eraser") return;
    if (!lite && s.glow && s.tool !== "highlighter" && drawGlowing(ctx, s, s.glow)) return;
    drawStrokeCore(ctx, s);
  }
  var glowBitmaps = /* @__PURE__ */ new Map();
  var glowBytes = 0;
  var GLOW_CACHE_BYTES = 96 * 1024 * 1024;
  var GLOW_CACHE_PIXELS = 25e5;
  function dropGlowBitmap(s) {
    const c = glowBitmaps.get(s);
    if (!c) return;
    glowBytes -= c.bytes;
    c.canvas.width = 0;
    c.canvas.height = 0;
    glowBitmaps.delete(s);
  }
  function drawGlowing(ctx, s, g) {
    const t = ctx.getTransform();
    const k = t.a;
    const hit = glowBitmaps.get(s);
    if (hit && hit.pts === s.pts && hit.widths === s.widths && hit.color === s.color && hit.size === g.size && hit.intensity === g.intensity && Math.abs(hit.k - k) < 1e-9) {
      glowBitmaps.delete(s);
      glowBitmaps.set(s, hit);
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.drawImage(hit.canvas, Math.round(t.e) + hit.ox, Math.round(t.f) + hit.oy);
      ctx.restore();
      return true;
    }
    if (hit) dropGlowBitmap(s);
    const reach = Math.ceil(g.size * k * 1.6 + maxOf(s.widths) * k + 2);
    const ox = Math.floor(s.bbox[0] * k) - reach, oy = Math.floor(s.bbox[1] * k) - reach;
    const w = Math.ceil((s.bbox[2] - s.bbox[0]) * k) + 2 * reach, h = Math.ceil((s.bbox[3] - s.bbox[1]) * k) + 2 * reach;
    if (!(w > 0 && h > 0) || w * h > MAX_GLOW_PIXELS) return false;
    const scratch = document.createElement("canvas");
    scratch.width = w;
    scratch.height = h;
    const sc = scratch.getContext("2d");
    sc.setTransform(k, 0, 0, k, -ox, -oy);
    drawStrokeCore(sc, s);
    if (w * h > GLOW_CACHE_PIXELS) {
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.filter = glowFilter(s.color, g, k);
      ctx.drawImage(scratch, Math.round(t.e) + ox, Math.round(t.f) + oy);
      ctx.restore();
      scratch.width = 0;
      return true;
    }
    const out = document.createElement("canvas");
    out.width = w;
    out.height = h;
    const oc = out.getContext("2d");
    oc.filter = glowFilter(s.color, g, k);
    oc.drawImage(scratch, 0, 0);
    scratch.width = 0;
    const bytes = w * h * 4;
    glowBitmaps.set(s, { pts: s.pts, widths: s.widths, color: s.color, size: g.size, intensity: g.intensity, k, canvas: out, ox, oy, bytes });
    glowBytes += bytes;
    while (glowBytes > GLOW_CACHE_BYTES && glowBitmaps.size > 1) dropGlowBitmap(glowBitmaps.keys().next().value);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.drawImage(out, Math.round(t.e) + ox, Math.round(t.f) + oy);
    ctx.restore();
    return true;
  }
  function drawStrokeCore(ctx, s) {
    ctx.setLineDash(s.dash ? DASHES[s.dash] ?? [] : []);
    const p = displayPoints(s, ctx.getTransform().a);
    const n = p.length >> 1;
    if (n === 0) return;
    ctx.strokeStyle = s.color;
    ctx.fillStyle = s.color;
    ctx.lineCap = s.cap;
    ctx.lineJoin = "round";
    const base = s.widths[0];
    if (n === 1) {
      ctx.beginPath();
      ctx.arc(p[0], p[1], Math.max(base, s.widths[1] ?? base) / 2, 0, Math.PI * 2);
      ctx.fill();
      return;
    }
    if (s.widths.length <= 1) {
      ctx.lineWidth = base;
      ctx.beginPath();
      ctx.moveTo(p[0], p[1]);
      for (let i = 1; i < n; i++) ctx.lineTo(p[2 * i], p[2 * i + 1]);
      ctx.stroke();
      return;
    }
    const step = Math.max(0.05, 0.33 / ctx.getTransform().a);
    const q = (v) => Math.max(0.05, Math.round(v / step) * step);
    let runW = q(s.widths[1] ?? base);
    ctx.lineWidth = runW;
    ctx.beginPath();
    ctx.moveTo(p[0], p[1]);
    for (let i = 1; i < n; i++) {
      const w = q(s.widths[i] ?? base);
      if (w !== runW) {
        ctx.stroke();
        runW = w;
        ctx.lineWidth = runW;
        ctx.beginPath();
        ctx.moveTo(p[2 * (i - 1)], p[2 * (i - 1) + 1]);
      }
      ctx.lineTo(p[2 * i], p[2 * i + 1]);
    }
    ctx.stroke();
  }
  function drawText(ctx, t) {
    if (isMarkdown(t)) {
      drawMarkdown(ctx, t);
      return;
    }
    const family = t.font === "Sans" ? "system-ui, sans-serif" : `"${t.font}", sans-serif`;
    ctx.font = `${t.size}px ${family}`;
    ctx.fillStyle = t.color;
    ctx.textBaseline = "top";
    const lh = t.size * 1.2;
    t.text.split("\n").forEach((line, i) => ctx.fillText(line, t.x, t.y + i * lh));
  }
  function drawImage(ctx, im, onLoaded) {
    if (im.bitmap) {
      ctx.drawImage(im.bitmap, im.left, im.top, im.right - im.left, im.bottom - im.top);
      return;
    }
    if (im.loading) return;
    im.loading = true;
    const bin = atob(im.b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    void createImageBitmap(new Blob([bytes])).then((b) => {
      im.bitmap = b;
      im.loading = false;
      onLoaded();
    }).catch(() => {
      im.loading = false;
    });
  }
  var TEX_PX_PER_PT = 4;
  var TEX_MAX_PX = 2048;
  var TEX_SHARP_MAX = 4096;
  function drawTexImage(ctx, t, onLoaded) {
    const w = t.right - t.left, h = t.bottom - t.top;
    if (t.bitmap) {
      const need = Math.ceil(w * ctx.getTransform().a);
      if (need > t.bitmap.width * 1.25 && t.bitmap.width < TEX_SHARP_MAX && !t.loading && !t.sharpFailed) {
        t.loading = true;
        const target = Math.min(TEX_SHARP_MAX, Math.max(need, Math.ceil(t.bitmap.width * 2)));
        void rasterizeAuto(t.pdfB64, target).then((cv) => {
          if (cv.width > 0) t.bitmap = cv;
          else t.sharpFailed = true;
          t.loading = false;
          onLoaded();
        }).catch(() => {
          t.loading = false;
          t.sharpFailed = true;
        });
      }
      const pose = texPose(t);
      if (pose.rot === 0 && !pose.flip) {
        ctx.drawImage(t.bitmap, t.left, t.top, w, h);
        return;
      }
      ctx.save();
      ctx.translate(t.left + w / 2, t.top + h / 2);
      ctx.rotate(pose.rot);
      if (pose.flip) ctx.scale(-1, 1);
      ctx.drawImage(t.bitmap, -w / 2, -h / 2, w, h);
      ctx.restore();
      return;
    }
    ctx.save();
    ctx.strokeStyle = t.failed ? "rgba(200,60,60,0.8)" : "rgba(90,90,90,0.6)";
    ctx.setLineDash([3, 3]);
    ctx.lineWidth = 0.7;
    ctx.strokeRect(t.left, t.top, w, h);
    ctx.restore();
    if (t.loading || t.failed) return;
    t.loading = true;
    void rasterizeAuto(t.pdfB64, Math.min(TEX_MAX_PX, Math.max(64, Math.round(w * TEX_PX_PER_PT)))).then((cv) => {
      t.bitmap = cv;
      t.loading = false;
      onLoaded();
    }).catch(() => {
      t.loading = false;
      t.failed = true;
      onLoaded();
    });
  }
  function drawElement(ctx, el, onImageLoaded, lite = false) {
    if (el.kind === "stroke") drawStroke(ctx, el, lite);
    else if (el.kind === "text") drawText(ctx, el);
    else if (el.kind === "image") drawImage(ctx, el, onImageLoaded);
    else if (el.kind === "teximage") drawTexImage(ctx, el, onImageLoaded);
  }
  function renderPage(ctx, page2, onImageLoaded, region, lite = false) {
    drawBackground(ctx, page2);
    for (const layer2 of page2.layers) {
      if (layer2.hidden) continue;
      for (const el of layer2.elements) {
        if (region) {
          const b = elementBBox(el);
          const pad = 6 + (el.kind === "stroke" && el.glow ? el.glow.size * 2 : 0);
          if (b && (b[2] + pad < region[0] || b[0] - pad > region[2] || b[3] + pad < region[1] || b[1] - pad > region[3])) continue;
        }
        drawElement(ctx, el, onImageLoaded, lite);
      }
    }
  }

  // ../../../app/src/renderer/journal.ts
  var PAPER_FORMATS = {
    A3: [841.889764, 1190.551181, "A3"],
    A4: [595.275591, 841.889764, "A4"],
    A5: [419.527559, 595.275591, "A5"],
    Letter: [612, 792, "Carta (Letter)"],
    Legal: [612, 1008, "Legal"]
  };
  var MM_TO_PT = 72 / 25.4;
  function blankDocument() {
    const [w, h] = PAPER_FORMATS.A4;
    const page2 = {
      attrs: [["width", fmt8(w)], ["height", fmt8(h)]],
      width: w,
      height: h,
      background: { attrs: [["type", "solid"], ["color", "#ffffffff"], ["style", "plain"]], type: "solid", color: cssColor("#ffffffff"), style: "plain" },
      layers: [{ attrs: [], elements: [] }]
    };
    return {
      rootAttrs: [["creator", "xournalpp 1.3.8"], ["fileversion", "4"]],
      title: "Xournal++ document - see https://xournalpp.github.io/",
      previewB64: null,
      pages: [page2],
      elementCount: 0,
      droppedChildren: 0
    };
  }

  // entry.ts
  window.xlp = {
    kinds: () => PAPER_KINDS.map((k) => ({ id: k.id, label: k.label, fallback: k.fallback, params: k.params })),
    /** One A4 page with the paper `raw` ("x:kind;k=v" or a built-in style) on `color`, serialized as the Electron app writes it. */
    make(value, color) {
      const doc = blankDocument();
      const bg = doc.pages[0].background;
      setAttr(bg.attrs, "color", color + "ff");
      bg.color = cssColor(color + "ff");
      let spec = null;
      if (value.startsWith("x:")) {
        const s = parseSpec(value.slice(2));
        const k = PAPER_KINDS.find((q) => q.id === s.kind);
        bg.paper = s;
        bg.style = k.fallback;
        setAttr(bg.attrs, "style", k.fallback);
        setAttr(bg.attrs, "xlPaper", formatSpec(s));
        spec = formatSpec(s);
      } else {
        bg.style = value;
        setAttr(bg.attrs, "style", value);
      }
      return { xml: serializeXopp(doc), spec };
    },
    /** Render page 0 of a document (xml) at `scale` to a PNG data URL. */
    render(xml, scale) {
      const doc = parseXopp(xml);
      const page2 = doc.pages[0];
      const cv = document.createElement("canvas");
      cv.width = Math.ceil(page2.width * scale);
      cv.height = Math.ceil(page2.height * scale);
      const ctx = cv.getContext("2d");
      ctx.scale(scale, scale);
      renderPage(ctx, page2, () => void 0);
      return cv.toDataURL("image/png");
    },
    defaults: () => PAPER_KINDS.map((k) => formatSpec(defaultSpec(k.id)))
  };
})();
