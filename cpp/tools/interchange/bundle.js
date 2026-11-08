(() => {
  // ../app/src/renderer/paper.ts
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

  // ../app/src/renderer/model.ts
  function attrValue(attrs2, name) {
    return attrs2.find((a) => a[0] === name)?.[1] ?? null;
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
  function readGlow(attrs2) {
    const size = Number(attrValue(attrs2, "glow"));
    if (!(size > 0)) return void 0;
    const a = Number(attrValue(attrs2, "glowAlpha") ?? "0.8");
    return { size: Math.min(GLOW_MAX_SIZE, size), intensity: Number.isFinite(a) ? Math.min(1, Math.max(0.05, a)) : 0.8 };
  }
  function readStroke(e) {
    const widths = nums(e.getAttribute("width") ?? "1.41");
    const pts = nums(e.textContent ?? "");
    if (pts.length & 1) pts.pop();
    const cap = e.getAttribute("capStyle");
    const attrs2 = attrsOf(e);
    const glow = readGlow(attrs2);
    return {
      kind: "stroke",
      attrs: attrs2,
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

  // ../app/src/renderer/serialize.ts
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
  function attrString(attrs2, override, fallback) {
    let out = "";
    const seen = /* @__PURE__ */ new Set();
    for (const [k, v] of attrs2) {
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

  // tools/interchange/entry.ts
  var sha = async (s) => {
    const t = s.replace(/\s+/g, "");
    let h = 2166136261;
    for (let i = 0; i < t.length; i++) {
      h ^= t.charCodeAt(i) & 255;
      h = Math.imul(h, 16777619) >>> 0;
    }
    return h.toString(16);
  };
  var attrs = (a, dropWidth = false) => a.filter((kv) => !(dropWidth && kv[0] === "width"));
  async function dump(xml) {
    const d = parseXopp(xml);
    const pages = [];
    for (const p of d.pages) {
      const layers = [];
      for (const l of p.layers) {
        const els = [];
        for (const e of l.elements) {
          if (e.kind === "stroke") els.push({ k: "stroke", attrs: attrs(e.attrs, true), widths: e.widths, pts: e.pts });
          else if (e.kind === "text") els.push({ k: "text", attrs: attrs(e.attrs), text: e.text });
          else if (e.kind === "image") els.push({ k: "image", attrs: attrs(e.attrs), sha: await sha(e.b64), len: e.b64.length });
          else if (e.kind === "teximage") els.push({ k: "teximage", attrs: attrs(e.attrs), sha: await sha(e.pdfB64), len: e.pdfB64.length });
          else els.push({ k: "UNSUPPORTED:" + e.tag });
        }
        layers.push({ attrs: attrs(l.attrs), els });
      }
      pages.push({ attrs: attrs(p.attrs), bg: attrs(p.background.attrs), layers });
    }
    return { root: attrs(d.rootAttrs), title: d.title ?? "", preview: d.previewB64 ? await sha(d.previewB64) : await sha(""), pages, dropped: d.droppedChildren };
  }
  function reserialize(xml) {
    return serializeXopp(parseXopp(xml));
  }
  window.xl = { dump, reserialize };
})();
