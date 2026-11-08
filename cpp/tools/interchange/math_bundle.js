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

  // ../../../app/src/renderer/model.ts
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
  function setGlow(s, g) {
    s.attrs = s.attrs.filter((a) => a[0] !== "glow" && a[0] !== "glowAlpha");
    if (g) {
      s.attrs.push(["glow", String(+g.size.toFixed(2))], ["glowAlpha", String(+g.intensity.toFixed(2))]);
      s.glow = { size: g.size, intensity: g.intensity };
    } else delete s.glow;
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
  function stampText(t) {
    setAttr(t.attrs, "font", t.font);
    setAttr(t.attrs, "size", fmt8(t.size));
    setAttr(t.attrs, "x", fmt8(t.x));
    setAttr(t.attrs, "y", fmt8(t.y));
    setAttr(t.attrs, "color", hexAlpha(t.color));
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
  var SNAP15 = Math.PI / 12;

  // ../../../app/src/renderer/plane.ts
  var DEFAULT_PLANE = { xmin: -5, xmax: 5, ymin: -5, ymax: 5, xstep: 1, ystep: 1, grid: true, sub: 0, ticks: true, numbers: true, arrows: true, xlabel: "x", ylabel: "y", square: true };
  var MAX_LINES = 120;
  var SUBS = [0, 2, 4, 5, 10];
  var finite = (v, d) => typeof v === "number" && Number.isFinite(v) ? v : d;
  var clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  var round6 = (v) => Math.round(v * 1e6) / 1e6;
  function sanitizePlane(raw) {
    const o = typeof raw === "object" && raw !== null ? raw : {};
    const d = DEFAULT_PLANE;
    const xstep = clamp(finite(o.xstep, d.xstep), 1e-6, 1e6) || d.xstep;
    const ystep = clamp(finite(o.ystep, d.ystep), 1e-6, 1e6) || d.ystep;
    let xmin = finite(o.xmin, d.xmin), xmax = finite(o.xmax, d.xmax), ymin = finite(o.ymin, d.ymin), ymax = finite(o.ymax, d.ymax);
    xmin = Math.floor(xmin / xstep + 1e-9) * xstep;
    xmax = Math.ceil(xmax / xstep - 1e-9) * xstep;
    ymin = Math.floor(ymin / ystep + 1e-9) * ystep;
    ymax = Math.ceil(ymax / ystep - 1e-9) * ystep;
    if (xmax <= xmin) xmax = xmin + xstep;
    if (ymax <= ymin) ymax = ymin + ystep;
    const sub = SUBS.includes(Number(o.sub)) ? Number(o.sub) : d.sub;
    const text3 = (v, def) => typeof v === "string" ? v.slice(0, 12) : def;
    const spec = {
      xmin: round6(xmin),
      xmax: round6(xmax),
      ymin: round6(ymin),
      ymax: round6(ymax),
      xstep: round6(xstep),
      ystep: round6(ystep),
      grid: o.grid !== false,
      sub,
      ticks: o.ticks !== false,
      numbers: o.numbers !== false,
      arrows: o.arrows !== false,
      xlabel: text3(o.xlabel, d.xlabel),
      ylabel: text3(o.ylabel, d.ylabel),
      square: o.square !== false
    };
    while ((spec.xmax - spec.xmin) / spec.xstep > MAX_LINES) {
      spec.xstep = round6(spec.xstep * 2);
      spec.xmin = Math.floor(spec.xmin / spec.xstep) * spec.xstep;
      spec.xmax = Math.ceil(spec.xmax / spec.xstep) * spec.xstep;
    }
    while ((spec.ymax - spec.ymin) / spec.ystep > MAX_LINES) {
      spec.ystep = round6(spec.ystep * 2);
      spec.ymin = Math.floor(spec.ymin / spec.ystep) * spec.ystep;
      spec.ymax = Math.ceil(spec.ymax / spec.ystep) * spec.ystep;
    }
    while (spec.sub && ((spec.xmax - spec.xmin) / spec.xstep + (spec.ymax - spec.ymin) / spec.ystep) * spec.sub > 600) spec.sub = SUBS[Math.max(0, SUBS.indexOf(spec.sub) - 1)];
    return spec;
  }
  function encodeSpec(s) {
    return JSON.stringify({ ...s, grid: s.grid ? 1 : 0, ticks: s.ticks ? 1 : 0, numbers: s.numbers ? 1 : 0, arrows: s.arrows ? 1 : 0, square: s.square ? 1 : 0 });
  }
  function decodeSpec(text3) {
    if (!text3) return null;
    try {
      const o = JSON.parse(text3);
      return sanitizePlane({ ...o, grid: o.grid !== 0, ticks: o.ticks !== 0, numbers: o.numbers !== 0, arrows: o.arrows !== 0, square: o.square !== 0 });
    } catch {
      return null;
    }
  }
  var attrsOfEl = (el) => "attrs" in el ? el.attrs : [];
  var planeIdOf = (el) => attrValue(attrsOfEl(el), "xlPlane");
  var roleOf = (el) => attrValue(attrsOfEl(el), "xlRole");
  function planeSpecOf(members) {
    for (const m of members) {
      const s = decodeSpec(attrValue(attrsOfEl(m), "xlPlaneSpec"));
      if (s) return s;
    }
    return null;
  }
  function planeFrameOf(members) {
    const ax = members.find((m) => m.kind === "stroke" && roleOf(m) === "axisx");
    const ay = members.find((m) => m.kind === "stroke" && roleOf(m) === "axisy");
    if (!ax || !ay) return null;
    const bx = bboxOf(ax.pts), by = bboxOf(ay.pts);
    return { x0: bx[0], x1: bx[2], y0: by[1], y1: by[3] };
  }
  function planeStyleOf(members) {
    const ax = members.find((m) => m.kind === "stroke" && roleOf(m) === "axisx");
    if (!ax) return null;
    const hex = (attrValue(ax.attrs, "color") ?? "#000000ff").slice(0, 7);
    const w = Number(attrValue(ax.attrs, "xlPlaneW"));
    return { color: hex, width: Number.isFinite(w) && w > 0 ? w : ax.widths[0] ?? 1.41, ...ax.glow ? { glow: { ...ax.glow } } : {} };
  }
  function multiples(min, max, step) {
    const out = [];
    const n0 = Math.round(min / step), n1 = Math.round(max / step);
    for (let n = n0; n <= n1; n++) out.push(round6(n * step));
    return out;
  }
  var fmtNum = (v) => {
    const s = String(round6(v));
    return s === "-0" ? "0" : s.replace("-", "\u2212");
  };
  function strokeEl(pts, color, alpha2, width, role, id, glow) {
    const colorAttr = color + alpha2;
    const s = {
      kind: "stroke",
      attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(+width.toFixed(3))], ["capStyle", "round"], ["xlPlane", id], ["xlRole", role]],
      tool: "pen",
      color: cssColor(colorAttr),
      widths: [width],
      pts,
      cap: "round",
      dash: null,
      bbox: bboxOf(pts)
    };
    if (glow) setGlow(s, glow);
    return s;
  }
  function textEl(text3, x, y, size, color, id, align, vcenter) {
    const t = { kind: "text", attrs: [["xlPlane", id], ["xlRole", "label"]], text: text3, font: "Sans", size, x, y, color: cssColor(color + "ff") };
    const b = textBBox(t);
    const w = b[2] - b[0], h = b[3] - b[1];
    t.x = align === "center" ? x - w / 2 : align === "right" ? x - w : x;
    t.y = vcenter ? y - h / 2 : y;
    stampText(t);
    return t;
  }
  function buildPlane(id, rawSpec, frame, style) {
    const spec = sanitizePlane(rawSpec);
    const { x0, y0, x1, y1 } = frame;
    const rx = spec.xmax - spec.xmin, ry = spec.ymax - spec.ymin;
    const px = (x) => x0 + (x - spec.xmin) / rx * (x1 - x0);
    const py = (y) => y1 - (y - spec.ymin) / ry * (y1 - y0);
    const w = Math.max(0.3, style.width);
    const out = [];
    const color = style.color;
    const xs = multiples(spec.xmin, spec.xmax, spec.xstep), ys = multiples(spec.ymin, spec.ymax, spec.ystep);
    const serpentine = (vals, vertical) => {
      const pts = [];
      vals.forEach((v, i) => {
        const a = vertical ? [px(v), i % 2 ? y0 : y1] : [i % 2 ? x1 : x0, py(v)];
        const b = vertical ? [px(v), i % 2 ? y1 : y0] : [i % 2 ? x0 : x1, py(v)];
        pts.push(a[0], a[1], b[0], b[1]);
      });
      return pts;
    };
    if (spec.grid) {
      if (spec.sub > 1) {
        const mx = multiples(spec.xmin, spec.xmax, spec.xstep / spec.sub).filter((v) => Math.abs(v / spec.xstep - Math.round(v / spec.xstep)) > 1e-6);
        const my = multiples(spec.ymin, spec.ymax, spec.ystep / spec.sub).filter((v) => Math.abs(v / spec.ystep - Math.round(v / spec.ystep)) > 1e-6);
        if (mx.length > 1) out.push(strokeEl(serpentine(mx, true), color, "33", Math.max(0.25, w * 0.3), "subgrid", id));
        if (my.length > 1) out.push(strokeEl(serpentine(my, false), color, "33", Math.max(0.25, w * 0.3), "subgrid", id));
      }
      if (xs.length > 1) out.push(strokeEl(serpentine(xs, true), color, "66", Math.max(0.3, w * 0.5), "grid", id));
      if (ys.length > 1) out.push(strokeEl(serpentine(ys, false), color, "66", Math.max(0.3, w * 0.5), "grid", id));
    }
    const ya = spec.ymin <= 0 && spec.ymax >= 0 ? py(0) : py(spec.ymin);
    const xa = spec.xmin <= 0 && spec.xmax >= 0 ? px(0) : px(spec.xmin);
    const tl = clamp(w * 0.9 + 1.2, 2, 3.6);
    const head2 = clamp(w * 1.4 + 3.5, 4.5, 7.5);
    const tickW = Math.max(0.3, w * 0.6), arrowW = Math.max(0.3, w * 0.8);
    const ax = strokeEl([x0, ya, x1, ya], color, "ff", w, "axisx", id, style.glow);
    ax.attrs.push(["xlPlaneSpec", encodeSpec(spec)], ["xlPlaneW", String(+w.toFixed(3))]);
    out.push(ax, strokeEl([xa, y1, xa, y0], color, "ff", w, "axisy", id, style.glow));
    if (spec.ticks) {
      const tx = [], ty = [];
      for (const v of xs) {
        const X = px(v);
        if (Math.abs(X - xa) < 0.01 || X <= x0 + 0.01 || X >= x1 - 0.01) continue;
        tx.push(X, ya, X, ya - tl, X, ya + tl, X, ya);
      }
      for (const v of [...ys].reverse()) {
        const Y = py(v);
        if (Math.abs(Y - ya) < 0.01 || Y >= y1 - 0.01 || Y <= y0 + 0.01) continue;
        ty.push(xa, Y, xa - tl, Y, xa + tl, Y, xa, Y);
      }
      if (tx.length) out.push(strokeEl(tx, color, "ff", tickW, "ticks", id, style.glow));
      if (ty.length) out.push(strokeEl(ty, color, "ff", tickW, "ticks", id, style.glow));
    }
    if (spec.arrows) {
      out.push(strokeEl([x1 - head2, ya - head2 * 0.38, x1, ya, x1 - head2, ya + head2 * 0.38], color, "ff", arrowW, "arrow", id, style.glow));
      out.push(strokeEl([xa - head2 * 0.38, y0 + head2, xa, y0, xa + head2 * 0.38, y0 + head2], color, "ff", arrowW, "arrow", id, style.glow));
    }
    const cellX = Math.abs((x1 - x0) * spec.xstep / rx), cellY = Math.abs((y1 - y0) * spec.ystep / ry);
    const size = clamp(Math.min(cellX, cellY) * 0.36, 6.5, 11);
    if (spec.numbers) {
      const widest = Math.max(...xs.map((v) => fmtNum(v).length)) * size * 0.62;
      const everyX = Math.max(1, Math.ceil(widest * 1.4 / cellX)), everyY = Math.max(1, Math.ceil(size * 1.7 / cellY));
      xs.forEach((v, i) => {
        if (Math.abs(px(v) - xa) < 0.01 || i % everyX) return;
        out.push(textEl(fmtNum(v), px(v), ya + tl + 1.5, size, color, id, "center", false));
      });
      ys.forEach((v, i) => {
        if (Math.abs(py(v) - ya) < 0.01 || i % everyY) return;
        out.push(textEl(fmtNum(v), xa - tl - 2, py(v), size, color, id, "right", true));
      });
    }
    const nameSize = clamp(size * 1.15, 8, 13);
    if (spec.xlabel) out.push(textEl(spec.xlabel, x1 + 3, ya, nameSize, color, id, "left", true));
    if (spec.ylabel) out.push(textEl(spec.ylabel, xa, y0 - nameSize * 1.25, nameSize, color, id, "center", false));
    return out;
  }

  // ../../../app/src/renderer/space3.ts
  var clamp2 = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  var finite2 = (v, d) => typeof v === "number" && Number.isFinite(v) ? v : d;
  var round62 = (v) => Math.round(v * 1e6) / 1e6;
  var MAX_LINES2 = 40;
  function axisVector(deg, len) {
    const a = deg * Math.PI / 180;
    return [Math.cos(a) * len, -Math.sin(a) * len];
  }
  function defaultVectors(scale = 1) {
    return [axisVector(215, 24 * scale), axisVector(0, 34 * scale), axisVector(90, 34 * scale)];
  }
  var DEFAULT_SPACE3 = {
    xmin: -3,
    xmax: 3,
    ymin: -3,
    ymax: 3,
    zmin: -3,
    zmax: 3,
    xstep: 1,
    ystep: 1,
    zstep: 1,
    gridXY: true,
    gridXZ: false,
    gridYZ: false,
    ticks: true,
    numbers: true,
    arrows: true,
    names: ["x", "y", "z"],
    o: [0, 0],
    u: defaultVectors(),
    n: defaultVectors()
  };
  var vec = (v, d) => Array.isArray(v) && v.length === 2 && v.every((n) => typeof n === "number" && Number.isFinite(n)) ? [v[0], v[1]] : [...d];
  function sanitizeSpace3(raw) {
    const o = typeof raw === "object" && raw !== null ? raw : {};
    const d = DEFAULT_SPACE3;
    const axis = (min, max, step, dmin, dmax, dstep) => {
      const st = clamp2(finite2(step, dstep), 1e-6, 1e6) || dstep;
      let lo = Math.floor(finite2(min, dmin) / st + 1e-9) * st, hi = Math.ceil(finite2(max, dmax) / st - 1e-9) * st;
      if (hi <= lo) hi = lo + st;
      let s = st;
      while ((hi - lo) / s > MAX_LINES2) {
        s *= 2;
        lo = Math.floor(lo / s) * s;
        hi = Math.ceil(hi / s) * s;
      }
      return { lo: round62(lo), hi: round62(hi), st: round62(s) };
    };
    const x = axis(o.xmin, o.xmax, o.xstep, d.xmin, d.xmax, d.xstep), y = axis(o.ymin, o.ymax, o.ystep, d.ymin, d.ymax, d.ystep), z = axis(o.zmin, o.zmax, o.zstep, d.zmin, d.zmax, d.zstep);
    const names = Array.isArray(o.names) ? o.names.map((n, i) => typeof n === "string" ? n.slice(0, 12) : d.names[i]) : [...d.names];
    const uu = Array.isArray(o.u) ? o.u : [];
    const nn = Array.isArray(o.n) ? o.n : [];
    const u = [vec(uu[0], d.u[0]), vec(uu[1], d.u[1]), vec(uu[2], d.u[2])];
    return {
      xmin: x.lo,
      xmax: x.hi,
      xstep: x.st,
      ymin: y.lo,
      ymax: y.hi,
      ystep: y.st,
      zmin: z.lo,
      zmax: z.hi,
      zstep: z.st,
      gridXY: o.gridXY !== false && o.gridXY !== 0,
      gridXZ: o.gridXZ === true || o.gridXZ === 1,
      gridYZ: o.gridYZ === true || o.gridYZ === 1,
      ticks: o.ticks !== false && o.ticks !== 0,
      numbers: o.numbers !== false && o.numbers !== 0,
      arrows: o.arrows !== false && o.arrows !== 0,
      names: [names[0] ?? "x", names[1] ?? "y", names[2] ?? "z"],
      o: vec(o.o, d.o),
      u,
      n: [vec(nn[0], u[0]), vec(nn[1], u[1]), vec(nn[2], u[2])]
      // a space saved before the halves were independent has straight axes
    };
  }
  var encodeSpace3 = (s) => JSON.stringify(s);
  function decodeSpace3(text3) {
    if (!text3) return null;
    try {
      return sanitizeSpace3(JSON.parse(text3));
    } catch {
      return null;
    }
  }
  function project(s, x, y, z) {
    const c = [x, y, z];
    let px = s.o[0], py = s.o[1];
    for (let i = 0; i < 3; i++) {
      const v = c[i], w = v >= 0 ? s.u[i] : s.n[i];
      px += v * w[0];
      py += v * w[1];
    }
    return [px, py];
  }
  var range = (s, i) => i === 0 ? [s.xmin, s.xmax, s.xstep] : i === 1 ? [s.ymin, s.ymax, s.ystep] : [s.zmin, s.zmax, s.zstep];
  var crossing = (s, i) => {
    const [lo, hi] = range(s, i);
    return clamp2(0, lo, hi);
  };
  function axisPoint(s, i, v) {
    const c = [crossing(s, 0), crossing(s, 1), crossing(s, 2)];
    c[i] = v;
    return project(s, c[0], c[1], c[2]);
  }
  function anchorsOf(s) {
    const out = [];
    for (const i of [0, 1, 2]) {
      const [lo, hi] = range(s, i), c = crossing(s, i);
      if (hi > c) out.push({ axis: i, side: "+", at: axisPoint(s, i, hi), value: hi });
      if (lo < c) out.push({ axis: i, side: "-", at: axisPoint(s, i, lo), value: lo });
    }
    return out;
  }
  function fmtNum2(v) {
    const t = String(round62(v));
    return t === "-0" ? "0" : t.replace("-", "\u2212");
  }
  function strokeEl2(pts, color, alpha2, width, role, id, glow) {
    const colorAttr = color + alpha2;
    const st = {
      kind: "stroke",
      attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(+width.toFixed(3))], ["capStyle", "round"], ["xlPlane", id], ["xlRole", role]],
      tool: "pen",
      color: cssColor(colorAttr),
      widths: [width],
      pts,
      cap: "round",
      dash: null,
      bbox: bboxOf(pts)
    };
    if (glow) setGlow(st, glow);
    return st;
  }
  function textEl2(text3, at2, size, color, id) {
    const t = { kind: "text", attrs: [["xlPlane", id], ["xlRole", "label"]], text: text3, font: "Sans", size, x: 0, y: 0, color: cssColor(color + "ff") };
    const b = textBBox(t);
    t.x = at2[0] - (b[2] - b[0]) / 2;
    t.y = at2[1] - (b[3] - b[1]) / 2;
    stampText(t);
    return t;
  }
  function multiples2(min, max, step) {
    const out = [];
    for (let n = Math.round(min / step); n <= Math.round(max / step); n++) out.push(round62(n * step));
    return out;
  }
  function buildSpace3(id, rawSpec, style) {
    const s = sanitizeSpace3(rawSpec);
    const w = Math.max(0.3, style.width), color = style.color;
    const out = [];
    const cross = [crossing(s, 0), crossing(s, 1), crossing(s, 2)];
    const planes = [[s.gridXY, 0, 1], [s.gridXZ, 0, 2], [s.gridYZ, 1, 2]];
    for (const [on, a, b] of planes) {
      if (!on) continue;
      const third = [0, 1, 2].find((k) => k !== a && k !== b);
      const [alo, ahi, ast] = range(s, a), [blo, bhi, bst] = range(s, b);
      const pt = (va, vb) => {
        const c = [...cross];
        c[a] = va;
        c[b] = vb;
        c[third] = cross[third];
        return project(s, c[0], c[1], c[2]);
      };
      const along = (vals, fixedIsA) => {
        const pts = [];
        vals.forEach((v, i) => {
          const from = fixedIsA ? i % 2 ? bhi : blo : i % 2 ? ahi : alo, to = fixedIsA ? i % 2 ? blo : bhi : i % 2 ? alo : ahi;
          const p0 = fixedIsA ? pt(v, from) : pt(from, v), p1 = fixedIsA ? pt(v, to) : pt(to, v);
          pts.push(p0[0], p0[1], p1[0], p1[1]);
        });
        return pts;
      };
      const as = multiples2(alo, ahi, ast), bs = multiples2(blo, bhi, bst);
      if (as.length > 1) out.push(strokeEl2(along(as, true), color, "55", Math.max(0.3, w * 0.45), "grid3", id));
      if (bs.length > 1) out.push(strokeEl2(along(bs, false), color, "55", Math.max(0.3, w * 0.45), "grid3", id));
    }
    const tl = clamp2(w * 0.9 + 1.2, 2, 3.6), head2 = clamp2(w * 1.4 + 3.5, 4.5, 7.5);
    const tickW = Math.max(0.3, w * 0.6), arrowW = Math.max(0.3, w * 0.8);
    const roles = ["axis3x", "axis3y", "axis3z"];
    const labels = [];
    const ticks = [], arrows = [];
    const letters = s.names;
    const normalOf = (d) => {
      const l = Math.hypot(d[0], d[1]) || 1;
      let n = [-d[1] / l, d[0] / l];
      if (n[1] < -0.3 || Math.abs(n[1]) <= 0.3 && n[0] > 0) n = [-n[0], -n[1]];
      return n;
    };
    [0, 1, 2].forEach((i) => {
      const [lo, hi, st] = range(s, i);
      const p0 = axisPoint(s, i, lo), p1 = axisPoint(s, i, hi), pc = axisPoint(s, i, cross[i]);
      const axis = strokeEl2([p0[0], p0[1], pc[0], pc[1], p1[0], p1[1]], color, "ff", w, roles[i], id, style.glow);
      if (i === 0) axis.attrs.push(["xlSpace3", encodeSpace3(s)], ["xlPlaneW", String(+w.toFixed(3))]);
      out.push(axis);
      const dir = hi > cross[i] ? [p1[0] - pc[0], p1[1] - pc[1]] : [p1[0] - p0[0], p1[1] - p0[1]];
      const dl = Math.hypot(dir[0], dir[1]) || 1;
      const nrm = normalOf(dir);
      const vals = multiples2(lo, hi, st).filter((v) => v !== cross[i] && v !== lo && v !== hi);
      if (s.ticks && vals.length) {
        const pts = [];
        for (const v of vals) {
          const p = axisPoint(s, i, v);
          pts.push(p[0], p[1], p[0] - nrm[0] * tl, p[1] - nrm[1] * tl, p[0] + nrm[0] * tl, p[1] + nrm[1] * tl, p[0], p[1]);
        }
        ticks.push(strokeEl2(pts, color, "ff", tickW, "ticks", id, style.glow));
      }
      if (s.arrows) {
        const ux = dir[0] / dl, uy = dir[1] / dl;
        arrows.push(strokeEl2([p1[0] - ux * head2 - uy * head2 * 0.38, p1[1] - uy * head2 + ux * head2 * 0.38, p1[0], p1[1], p1[0] - ux * head2 + uy * head2 * 0.38, p1[1] - uy * head2 - ux * head2 * 0.38], color, "ff", arrowW, "arrow", id, style.glow));
      }
      const stepPx = Math.hypot(s.u[i][0], s.u[i][1]) * st;
      const size = clamp2(stepPx * 0.34, 6.5, 11);
      if (s.numbers) {
        const every = Math.max(1, Math.ceil(size * 2.2 / Math.max(stepPx, 1)));
        multiples2(lo, hi, st).forEach((v, k) => {
          if (v === cross[i] || k % every) return;
          const p = axisPoint(s, i, v);
          labels.push(textEl2(fmtNum2(v), [p[0] + nrm[0] * (tl + size * 0.7), p[1] + nrm[1] * (tl + size * 0.7)], size, color, id));
        });
      }
      if (letters[i]) labels.push(textEl2(letters[i], [p1[0] + dir[0] / dl * (head2 + 7), p1[1] + dir[1] / dl * (head2 + 7)], clamp2(size * 1.2, 8, 13), color, id));
    });
    return [...out, ...ticks, ...arrows, ...labels];
  }
  function axesOf(members) {
    return ["axis3x", "axis3y", "axis3z"].map((r) => members.find((m) => m.kind === "stroke" && roleOf(m) === r));
  }
  function isSpace3(members) {
    return axesOf(members).every(Boolean);
  }
  function space3SpecOf(members) {
    const axes = axesOf(members);
    const ax = axes[0];
    if (!axes.every(Boolean) || !ax) return null;
    const stored = decodeSpace3(attrValue(ax.attrs, "xlSpace3"));
    if (!stored) return null;
    const u = [], n = [];
    let cross = [0, 0];
    [0, 1, 2].forEach((i) => {
      const p = axes[i].pts;
      const [lo, hi] = range(stored, i), c = crossing(stored, i);
      cross = [p[2], p[3]];
      u.push(hi > c ? [(p[4] - p[2]) / (hi - c), (p[5] - p[3]) / (hi - c)] : stored.u[i]);
      n.push(lo < c ? [(p[0] - p[2]) / (lo - c), (p[1] - p[3]) / (lo - c)] : stored.n[i]);
    });
    const geo = { ...stored, u: [u[0], u[1], u[2]], n: [n[0], n[1], n[2]], o: [0, 0] };
    let ox = cross[0], oy = cross[1];
    for (const i of [0, 1, 2]) {
      const c = crossing(stored, i), w = c >= 0 ? geo.u[i] : geo.n[i];
      ox -= c * w[0];
      oy -= c * w[1];
    }
    geo.o = [ox, oy];
    return geo;
  }
  function space3StyleOf(members) {
    const ax = axesOf(members)[0];
    if (!ax) return null;
    const hex = (attrValue(ax.attrs, "color") ?? "#000000ff").slice(0, 7);
    const w = Number(attrValue(ax.attrs, "xlPlaneW"));
    return { color: hex, width: Number.isFinite(w) && w > 0 ? w : ax.widths[0] ?? 1.41, ...ax.glow ? { glow: { ...ax.glow } } : {} };
  }
  function placedSpec(base, scale, corner) {
    const grow = (a) => a.map((v) => [v[0] * scale, v[1] * scale]);
    const probe2 = { ...base, o: [0, 0], u: grow(base.u), n: grow(base.n) };
    const b = extentOf(probe2);
    return { ...probe2, o: [corner[0] - b[0], corner[1] - b[1]] };
  }
  function extentOf(s) {
    const pts = [];
    for (const x of [s.xmin, s.xmax]) for (const y of [s.ymin, s.ymax]) for (const z of [s.zmin, s.zmax]) pts.push(...project(s, x, y, z));
    return bboxOf(pts);
  }

  // ../../../app/src/renderer/genobj.ts
  function strokeEl3(pts, color, alpha2, width, role, id, dash, glow) {
    const colorAttr = color + alpha2;
    const st = {
      kind: "stroke",
      attrs: [["tool", "pen"], ["color", colorAttr], ["width", String(+width.toFixed(3))], ["capStyle", "round"], ["xlPlane", id], ["xlRole", role]],
      tool: "pen",
      color: cssColor(colorAttr),
      widths: [width],
      pts,
      cap: "round",
      dash: null,
      bbox: bboxOf(pts)
    };
    if (dash && dash !== "solid") {
      st.dash = dash;
      st.attrs.push(["style", dash]);
    }
    if (glow) setGlow(st, glow);
    return st;
  }
  function textEl3(text3, at2, size, color, id, role) {
    const t = { kind: "text", attrs: [["xlPlane", id], ["xlRole", role]], text: text3, font: "Sans", size, x: 0, y: 0, color: cssColor(color + "ff") };
    const b = textBBox(t);
    t.x = at2[0] - (b[2] - b[0]) / 2;
    t.y = at2[1] - (b[3] - b[1]) / 2;
    stampText(t);
    return t;
  }
  function head(tip, from, len) {
    const a = Math.atan2(tip[1] - from[1], tip[0] - from[0]);
    const b1 = a + Math.PI - 0.42, b2 = a + Math.PI + 0.42;
    return [tip[0] + Math.cos(b1) * len, tip[1] + Math.sin(b1) * len, tip[0], tip[1], tip[0] + Math.cos(b2) * len, tip[1] + Math.sin(b2) * len];
  }
  function affineFrom(src, dst) {
    const [x0, y0, x1, y1, x2, y2] = src;
    const det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (Math.abs(det) < 1e-9) return null;
    const solve = (v0, v1, v2) => {
      const a2 = ((v1 - v0) * (y2 - y0) - (v2 - v0) * (y1 - y0)) / det, b2 = ((v2 - v0) * (x1 - x0) - (v1 - v0) * (x2 - x0)) / det;
      return [a2, b2, v0 - a2 * x0 - b2 * y0];
    };
    const [a, c, e] = solve(dst[0], dst[2], dst[4]), [b, d, f] = solve(dst[1], dst[3], dst[5]);
    return [a, b, c, d, e, f];
  }
  function mapPoints(T, pts) {
    const out = [];
    for (let i = 0; i + 1 < pts.length; i += 2) out.push(T[0] * pts[i] + T[2] * pts[i + 1] + T[4], T[1] * pts[i] + T[3] * pts[i + 1] + T[5]);
    return out;
  }
  function findWitness(members, role, attr, roleOf2) {
    return members.find((m) => m.kind === "stroke" && roleOf2(m) === role && attrValue(m.attrs, attr) !== null) ?? null;
  }
  function makeWitness(w, role, attr, payload, color, id) {
    const wit = strokeEl3(w, color, "00", 0.3, role, id, null);
    wit.attrs.push([attr, JSON.stringify(payload)]);
    return wit;
  }

  // ../../../app/src/renderer/figures.ts
  var textEl4 = (text3, at2, size, color, id) => textEl3(text3, at2, size, color, id, "figlabel");
  var FIGURES = [
    { kind: "segment", name: "Segmento", group: "L\xEDneas", clicks: false },
    { kind: "arrow", name: "Flecha / vector", group: "L\xEDneas", clicks: false },
    { kind: "dim", name: "Cota", group: "L\xEDneas", clicks: false },
    { kind: "bezier2", name: "Curva cuadr\xE1tica", group: "Curvas", clicks: false },
    { kind: "bezier3", name: "Curva c\xFAbica", group: "Curvas", clicks: false },
    { kind: "spline", name: "Curva por puntos", group: "Curvas", clicks: true },
    { kind: "arc", name: "Arco", group: "Curvas", clicks: false },
    { kind: "polyline", name: "Poligonal", group: "Pol\xEDgonos", clicks: true },
    { kind: "polygon", name: "Pol\xEDgono", group: "Pol\xEDgonos", clicks: true },
    { kind: "regular", name: "Pol\xEDgono regular", group: "Pol\xEDgonos", clicks: false },
    { kind: "angle", name: "\xC1ngulo", group: "Pol\xEDgonos", clicks: false },
    { kind: "box", name: "Cubo / prisma", group: "3D", clicks: false },
    { kind: "pyramid", name: "Pir\xE1mide", group: "3D", clicks: false },
    { kind: "cylinder", name: "Cilindro", group: "3D", clicks: false },
    { kind: "cone", name: "Cono", group: "3D", clicks: false },
    { kind: "sphere", name: "Esfera", group: "3D", clicks: false }
  ];
  var clamp3 = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  var finite3 = (v, d) => typeof v === "number" && Number.isFinite(v) ? v : d;
  function pointRange(k) {
    switch (k) {
      case "segment":
      case "arrow":
      case "regular":
        return [2, 2];
      case "dim":
      case "bezier2":
      case "arc":
      case "angle":
        return [3, 3];
      case "bezier3":
        return [4, 4];
      case "polyline":
      case "spline":
        return [2, 200];
      case "polygon":
        return [3, 200];
      default:
        return [4, 4];
    }
  }
  function sanitizeFigure(raw) {
    const o = typeof raw === "object" && raw !== null ? raw : {};
    const kind = FIGURES.some((f) => f.kind === o.kind) ? o.kind : "segment";
    const [lo, hi] = pointRange(kind);
    const src = Array.isArray(o.pts) ? o.pts.map((v) => finite3(v, 0)) : [];
    const pts = [];
    for (let i = 0; i + 1 < src.length && pts.length / 2 < hi; i += 2) pts.push(src[i], src[i + 1]);
    while (pts.length / 2 < lo) pts.push(pts[pts.length - 2] ?? 0, (pts[pts.length - 1] ?? 0) + 30 * (pts.length / 2));
    return {
      kind,
      pts,
      heads: o.heads === "both" || o.heads === "end" ? o.heads : kind === "arrow" ? "end" : "none",
      dash: o.dash === "dash" || o.dash === "dot" ? o.dash : "solid",
      n: Math.round(clamp3(finite3(o.n, kind === "regular" ? 5 : kind === "sphere" ? 8 : 4), 3, 12)),
      m: Math.round(clamp3(finite3(o.m, 3), 0, 9)),
      hidden: o.hidden === "none" || o.hidden === "solid" ? o.hidden : "dash",
      label: typeof o.label === "string" ? o.label.slice(0, 40) : ""
    };
  }
  function viewVectors(scale) {
    const a = (deg, len) => [Math.cos(deg * Math.PI / 180) * len, -Math.sin(deg * Math.PI / 180) * len];
    return [a(215, 30 * scale), a(0, 48 * scale), a(90, 48 * scale)];
  }
  function defaultSpec2(kind, a, b, shift = false) {
    const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.hypot(dx, dy) || 1;
    let end = b;
    if (shift) {
      const ang = Math.round(Math.atan2(dy, dx) / (Math.PI / 12)) * (Math.PI / 12);
      end = [a[0] + Math.cos(ang) * len, a[1] + Math.sin(ang) * len];
    }
    const px = -(end[1] - a[1]) / len, py = (end[0] - a[0]) / len;
    let pts = [];
    switch (kind) {
      case "segment":
      case "arrow":
        pts = [a[0], a[1], end[0], end[1]];
        break;
      case "regular":
        pts = [a[0], a[1], end[0], end[1]];
        break;
      case "dim":
        pts = [a[0], a[1], end[0], end[1], (a[0] + end[0]) / 2 + px * 22, (a[1] + end[1]) / 2 + py * 22];
        break;
      case "bezier2":
        pts = [a[0], a[1], (a[0] + end[0]) / 2 + px * len * 0.4, (a[1] + end[1]) / 2 + py * len * 0.4, end[0], end[1]];
        break;
      case "bezier3":
        pts = [a[0], a[1], a[0] + (end[0] - a[0]) / 3 + px * len * 0.4, a[1] + (end[1] - a[1]) / 3 + py * len * 0.4, a[0] + 2 * (end[0] - a[0]) / 3 + px * len * 0.4, a[1] + 2 * (end[1] - a[1]) / 3 + py * len * 0.4, end[0], end[1]];
        break;
      case "arc":
        pts = [a[0], a[1], end[0], end[1], a[0] - (end[1] - a[1]), a[1] + (end[0] - a[0])];
        break;
      // 90 degrees, counter-clockwise on the page
      case "angle": {
        const c = Math.cos(-Math.PI / 3), s = Math.sin(-Math.PI / 3);
        pts = [a[0], a[1], end[0], end[1], a[0] + (end[0] - a[0]) * c - (end[1] - a[1]) * s, a[1] + (end[0] - a[0]) * s + (end[1] - a[1]) * c];
        break;
      }
      case "polyline":
      case "spline":
        pts = [a[0], a[1], end[0], end[1]];
        break;
      case "polygon":
        pts = [a[0], a[1], end[0], end[1], a[0] + (end[0] - a[0]) / 2 + px * len * 0.7, a[1] + (end[1] - a[1]) / 2 + py * len * 0.7];
        break;
      default: {
        const [e1, e2, e3] = viewVectors(Math.max(0.2, len / 90));
        pts = [a[0], a[1], a[0] + e1[0], a[1] + e1[1], a[0] + e2[0], a[1] + e2[1], a[0] + e3[0], a[1] + e3[1]];
      }
    }
    return sanitizeFigure({ kind, pts });
  }
  var P = (s, i) => [s.pts[2 * i], s.pts[2 * i + 1]];
  var flat = (a) => a.flatMap((p) => [p[0], p[1]]);
  function sampleBezier(p, steps) {
    const out = [];
    for (let i = 0; i <= steps; i++) {
      const t = i / steps, u = 1 - t;
      if (p.length === 3) out.push([u * u * p[0][0] + 2 * u * t * p[1][0] + t * t * p[2][0], u * u * p[0][1] + 2 * u * t * p[1][1] + t * t * p[2][1]]);
      else out.push([u * u * u * p[0][0] + 3 * u * u * t * p[1][0] + 3 * u * t * t * p[2][0] + t * t * t * p[3][0], u * u * u * p[0][1] + 3 * u * u * t * p[1][1] + 3 * u * t * t * p[2][1] + t * t * t * p[3][1]]);
    }
    return out;
  }
  function sampleSpline(p) {
    if (p.length < 3) return p.slice();
    const out = [];
    for (let i = 0; i < p.length - 1; i++) {
      const p0 = p[Math.max(0, i - 1)], p1 = p[i], p2 = p[i + 1], p3 = p[Math.min(p.length - 1, i + 2)];
      const steps = clamp3(Math.round(Math.hypot(p2[0] - p1[0], p2[1] - p1[1]) / 4), 6, 40);
      for (let s = 0; s < steps; s++) {
        const t = s / steps, t2 = t * t, t3 = t2 * t;
        out.push([
          0.5 * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3),
          0.5 * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
        ]);
      }
    }
    out.push(p[p.length - 1]);
    return out;
  }
  var angleOf = (c, p) => Math.atan2(-(p[1] - c[1]), p[0] - c[0]);
  var onCircle = (c, r, a) => [c[0] + Math.cos(a) * r, c[1] - Math.sin(a) * r];
  function arcPoints(c, r, a0, sweep) {
    const steps = clamp3(Math.round(Math.abs(sweep) * 180 / Math.PI / 3), 8, 120);
    const out = [];
    for (let i = 0; i <= steps; i++) out.push(onCircle(c, r, a0 + sweep * i / steps));
    return out;
  }
  var ccw = (from, to) => {
    let d = to - from;
    while (d <= 0) d += Math.PI * 2;
    return d;
  };
  function solidPoint(s, a, b, c) {
    const o = P(s, 0), e1 = P(s, 1), e2 = P(s, 2), e3 = P(s, 3);
    return [o[0] + a * (e1[0] - o[0]) + b * (e2[0] - o[0]) + c * (e3[0] - o[0]), o[1] + a * (e1[1] - o[1]) + b * (e2[1] - o[1]) + c * (e3[1] - o[1])];
  }
  function viewDir(s) {
    const o = P(s, 0), e1 = P(s, 1), e2 = P(s, 2), e3 = P(s, 3);
    const r1 = [e1[0] - o[0], e2[0] - o[0], e3[0] - o[0]], r2 = [e1[1] - o[1], e2[1] - o[1], e3[1] - o[1]];
    let d = [r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]];
    const l = Math.hypot(...d);
    if (l < 1e-9) return null;
    d = [d[0] / l, d[1] / l, d[2] / l];
    return d[0] < 0 ? [-d[0], -d[1], -d[2]] : d;
  }
  function polyhedron(kind) {
    if (kind === "box") {
      const verts = [];
      for (let c = 0; c < 2; c++) for (let b = 0; b < 2; b++) for (let a = 0; a < 2; a++) verts.push([a, b, c]);
      const v = (a, b, c) => a + 2 * b + 4 * c;
      return { verts, faces: [[v(0, 0, 0), v(0, 1, 0), v(0, 1, 1), v(0, 0, 1)], [v(1, 0, 0), v(1, 1, 0), v(1, 1, 1), v(1, 0, 1)], [v(0, 0, 0), v(1, 0, 0), v(1, 0, 1), v(0, 0, 1)], [v(0, 1, 0), v(1, 1, 0), v(1, 1, 1), v(0, 1, 1)], [v(0, 0, 0), v(1, 0, 0), v(1, 1, 0), v(0, 1, 0)], [v(0, 0, 1), v(1, 0, 1), v(1, 1, 1), v(0, 1, 1)]] };
    }
    return { verts: [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0.5, 0.5, 1]], faces: [[0, 1, 2, 3], [0, 1, 4], [1, 2, 4], [2, 3, 4], [3, 0, 4]] };
  }
  function normals(p) {
    const cen = [0, 0, 0];
    for (const v of p.verts) {
      cen[0] += v[0] / p.verts.length;
      cen[1] += v[1] / p.verts.length;
      cen[2] += v[2] / p.verts.length;
    }
    return p.faces.map((f) => {
      const a = p.verts[f[0]], b = p.verts[f[1]], c = p.verts[f[2]];
      const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], w = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
      let n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
      const fc = [0, 0, 0];
      for (const i of f) {
        fc[0] += p.verts[i][0] / f.length;
        fc[1] += p.verts[i][1] / f.length;
        fc[2] += p.verts[i][2] / f.length;
      }
      if (n[0] * (fc[0] - cen[0]) + n[1] * (fc[1] - cen[1]) + n[2] * (fc[2] - cen[2]) < 0) n = [-n[0], -n[1], -n[2]];
      const l = Math.hypot(...n) || 1;
      return [n[0] / l, n[1] / l, n[2] / l];
    });
  }
  function splitByDepth(path, depth, toPage) {
    const front = [], back = [];
    let run2 = [], inFront = depth(path[0]) >= 0;
    const flush = () => {
      if (run2.length >= 4) (inFront ? front : back).push(run2);
      run2 = [];
    };
    path.forEach((p, i) => {
      const dp = depth(p), f = dp >= 0;
      if (i > 0 && f !== inFront) {
        const q = path[i - 1], dq = depth(q), t = dq / (dq - dp);
        const x = toPage([q[0] + (p[0] - q[0]) * t, q[1] + (p[1] - q[1]) * t, q[2] + (p[2] - q[2]) * t]);
        run2.push(x[0], x[1]);
        flush();
        inFront = f;
        run2 = [x[0], x[1]];
      }
      const pg = toPage(p);
      run2.push(pg[0], pg[1]);
    });
    flush();
    return { front, back };
  }
  function solidLines(s) {
    const solid = [], hidden = [];
    const d = viewDir(s);
    const pp = (p) => solidPoint(s, p[0], p[1], p[2]);
    if (s.kind === "box" || s.kind === "pyramid") {
      const poly = polyhedron(s.kind), nrm = normals(poly);
      const vis = nrm.map((n) => d ? n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > 1e-9 : true);
      const edges = /* @__PURE__ */ new Map();
      poly.faces.forEach((f, fi) => f.forEach((v, k) => {
        const w = f[(k + 1) % f.length];
        const key = v < w ? `${v}-${w}` : `${w}-${v}`;
        const e = edges.get(key) ?? { a: Math.min(v, w), b: Math.max(v, w), faces: [] };
        e.faces.push(fi);
        edges.set(key, e);
      }));
      for (const e of edges.values()) {
        const line = flat([pp(poly.verts[e.a]), pp(poly.verts[e.b])]);
        (e.faces.some((fi) => vis[fi]) ? solid : hidden).push(line);
      }
      return { solid, hidden };
    }
    const dd = d ?? [1, 0, 0];
    const side = (p) => p[0] * dd[0] + p[1] * dd[1];
    const circle2 = (h, steps = 72) => {
      const out = [];
      for (let i = 0; i <= steps; i++) {
        const t = Math.PI * 2 * i / steps;
        out.push([Math.cos(t), Math.sin(t), h]);
      }
      return out;
    };
    if (s.kind === "cylinder" || s.kind === "cone") {
      const pc = (p) => solidPoint(s, 0.5 + 0.5 * p[0], 0.5 + 0.5 * p[1], p[2]);
      const cone = s.kind === "cone";
      const topFaces = dd[2] > 1e-9;
      const addCircle = (h, capFacesViewer) => {
        const path = circle2(h);
        if (capFacesViewer) {
          solid.push(flat(path.map(pc)));
          return;
        }
        const { front, back } = splitByDepth(path, side, pc);
        solid.push(...front);
        hidden.push(...back);
      };
      addCircle(0, !topFaces);
      if (!cone) addCircle(1, topFaces);
      if (!cone) {
        const th = Math.atan2(-dd[0], dd[1]);
        for (const t of [th, th + Math.PI]) solid.push(flat([pc([Math.cos(t), Math.sin(t), 0]), pc([Math.cos(t), Math.sin(t), 1])]));
      } else {
        const apex = pc([0, 0, 1]), c0 = pc([0, 0, 0]), ex = pc([1, 0, 0]), ey = pc([0, 1, 0]);
        const m = [[ex[0] - c0[0], ey[0] - c0[0]], [ex[1] - c0[1], ey[1] - c0[1]]];
        const det2 = m[0][0] * m[1][1] - m[0][1] * m[1][0];
        if (Math.abs(det2) > 1e-9) {
          const rx = apex[0] - c0[0], ry = apex[1] - c0[1];
          const ax = (m[1][1] * rx - m[0][1] * ry) / det2, ay = (-m[1][0] * rx + m[0][0] * ry) / det2;
          const dist = Math.hypot(ax, ay);
          if (dist > 1.0001) {
            const al = Math.atan2(ay, ax), dl = Math.acos(1 / dist);
            for (const t of [al - dl, al + dl]) solid.push(flat([pc([Math.cos(t), Math.sin(t), 0]), apex]));
          }
        }
      }
      return { solid, hidden };
    }
    const ps = (p) => solidPoint(s, 0.5 + 0.5 * p[0], 0.5 + 0.5 * p[1], 0.5 + 0.5 * p[2]);
    const depth3 = (p) => p[0] * dd[0] + p[1] * dd[1] + p[2] * dd[2];
    const addPath = (path) => {
      const { front, back } = splitByDepth(path, depth3, ps);
      solid.push(...front);
      hidden.push(...back);
    };
    addPath(circle2(0));
    for (let j = 1; j <= s.m; j++) {
      const phi = -Math.PI / 2 + Math.PI * j / (s.m + 1);
      if (Math.abs(phi) < 1e-6) continue;
      const r = Math.cos(phi), z = Math.sin(phi);
      addPath(circle2(0, 72).map((p) => [p[0] * r, p[1] * r, z]));
    }
    for (let k = 0; k < s.n; k++) {
      const th = Math.PI * k / s.n;
      const path = [];
      for (let i = 0; i <= 72; i++) {
        const t = Math.PI * 2 * i / 72;
        path.push([Math.cos(t) * Math.cos(th), Math.cos(t) * Math.sin(th), Math.sin(t)]);
      }
      addPath(path);
    }
    const c = ps([0, 0, 0]), x1 = ps([1, 0, 0]), y1 = ps([0, 1, 0]), z1 = ps([0, 0, 1]);
    const a = [[x1[0] - c[0], y1[0] - c[0], z1[0] - c[0]], [x1[1] - c[1], y1[1] - c[1], z1[1] - c[1]]];
    const sxx = a[0][0] ** 2 + a[0][1] ** 2 + a[0][2] ** 2, syy = a[1][0] ** 2 + a[1][1] ** 2 + a[1][2] ** 2, sxy = a[0][0] * a[1][0] + a[0][1] * a[1][1] + a[0][2] * a[1][2];
    const tr = sxx + syy, det = sxx * syy - sxy * sxy, disc = Math.sqrt(Math.max(0, tr * tr / 4 - det));
    const l1 = tr / 2 + disc, l2 = Math.max(0, tr / 2 - disc);
    const ang = 0.5 * Math.atan2(2 * sxy, sxx - syy);
    const outline = [];
    for (let i = 0; i <= 96; i++) {
      const t = Math.PI * 2 * i / 96;
      const u = Math.cos(t) * Math.sqrt(l1), v = Math.sin(t) * Math.sqrt(l2);
      outline.push(c[0] + u * Math.cos(ang) - v * Math.sin(ang), c[1] + u * Math.sin(ang) + v * Math.cos(ang));
    }
    solid.push(outline);
    return { solid, hidden };
  }
  function witnessOf(s) {
    const a = P(s, 0), b = P(s, 1);
    let c = s.pts.length >= 6 ? P(s, 2) : [a[0], a[1]];
    const area = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
    const span = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    if (area < span * 3) c = [a[0] - (b[1] - a[1]) / span * 4, a[1] + (b[0] - a[0]) / span * 4];
    return [a[0], a[1], b[0], b[1], c[0], c[1]];
  }
  function buildFigure(id, rawSpec, style) {
    const s = sanitizeFigure(rawSpec);
    const w = Math.max(0.3, style.width), color = style.color;
    const out = [];
    const dash = s.dash;
    const body = (pts2, role = "figbody") => out.push(strokeEl3(pts2, color, "ff", w, role, id, dash, style.glow));
    const arrowW = Math.max(0.3, w * 0.9);
    const headLen = clamp3(w * 3 + 5, 6, 14);
    const addHeads = (path) => {
      if (s.heads === "none" || path.length < 2) return;
      const last = path[path.length - 1], prev = path[Math.max(0, path.length - 4)];
      if (s.heads === "end" || s.heads === "both") out.push(strokeEl3(head(last, prev, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow));
      if (s.heads === "both") out.push(strokeEl3(head(path[0], path[Math.min(path.length - 1, 3)], headLen), color, "ff", arrowW, "figarrow", id, null, style.glow));
    };
    const labelAt = (at2) => {
      if (s.label) out.push(textEl4(s.label, at2, 12, color, id));
    };
    const pts = Array.from({ length: s.pts.length / 2 }, (_, i) => P(s, i));
    switch (s.kind) {
      case "segment":
      case "arrow": {
        body(flat(pts));
        addHeads(pts);
        labelAt([(pts[0][0] + pts[1][0]) / 2 + 10, (pts[0][1] + pts[1][1]) / 2 - 10]);
        break;
      }
      case "polyline":
      case "polygon": {
        const path = s.kind === "polygon" ? [...pts, pts[0]] : pts;
        body(flat(path));
        if (s.kind === "polyline") addHeads(path);
        labelAt([pts.reduce((a, p) => a + p[0], 0) / pts.length, pts.reduce((a, p) => a + p[1], 0) / pts.length]);
        break;
      }
      case "regular": {
        const c = pts[0], v0 = pts[1];
        const verts = [];
        for (let k = 0; k < s.n; k++) {
          const t = 2 * Math.PI * k / s.n;
          verts.push([c[0] + (v0[0] - c[0]) * Math.cos(t) - (v0[1] - c[1]) * Math.sin(t), c[1] + (v0[0] - c[0]) * Math.sin(t) + (v0[1] - c[1]) * Math.cos(t)]);
        }
        body(flat([...verts, verts[0]]));
        labelAt([c[0], c[1]]);
        break;
      }
      case "bezier2":
      case "bezier3": {
        const chord2 = Math.hypot(pts[pts.length - 1][0] - pts[0][0], pts[pts.length - 1][1] - pts[0][1]);
        const path = sampleBezier(pts, clamp3(Math.round(chord2 / 4), 24, 160));
        body(flat(path));
        addHeads(path);
        labelAt([path[Math.floor(path.length / 2)][0] + 10, path[Math.floor(path.length / 2)][1] - 10]);
        break;
      }
      case "spline": {
        const path = sampleSpline(pts);
        body(flat(path));
        addHeads(path);
        labelAt([pts[Math.floor(pts.length / 2)][0] + 10, pts[Math.floor(pts.length / 2)][1] - 10]);
        break;
      }
      case "arc": {
        const c = pts[0], r = Math.hypot(pts[1][0] - c[0], pts[1][1] - c[1]);
        const a0 = angleOf(c, pts[1]), sweep = ccw(a0, angleOf(c, pts[2]));
        const path = arcPoints(c, r, a0, sweep);
        body(flat(path));
        addHeads(path);
        const mid = onCircle(c, r + 12, a0 + sweep / 2);
        labelAt(mid);
        break;
      }
      case "angle": {
        const v = pts[0], a = pts[1], b = pts[2];
        body(flat([a, v, b]));
        const la = Math.hypot(a[0] - v[0], a[1] - v[1]), lb = Math.hypot(b[0] - v[0], b[1] - v[1]);
        const r = Math.max(8, Math.min(la, lb) * 0.28);
        const a0 = angleOf(v, a), a1 = angleOf(v, b);
        let sweep = ccw(a0, a1);
        let from = a0;
        if (sweep > Math.PI) {
          from = a1;
          sweep = Math.PI * 2 - sweep;
        }
        if (Math.abs(sweep - Math.PI / 2) < 0.03) {
          const u = [Math.cos(from), -Math.sin(from)], t = [Math.cos(from + sweep), -Math.sin(from + sweep)];
          const k = r * 0.8;
          out.push(strokeEl3([v[0] + u[0] * k, v[1] + u[1] * k, v[0] + (u[0] + t[0]) * k, v[1] + (u[1] + t[1]) * k, v[0] + t[0] * k, v[1] + t[1] * k], color, "ff", Math.max(0.3, w * 0.7), "figmark", id, null, style.glow));
        } else {
          out.push(strokeEl3(flat(arcPoints(v, r, from, sweep)), color, "ff", Math.max(0.3, w * 0.7), "figmark", id, null, style.glow));
        }
        labelAt(onCircle(v, r * 1.9 + 4, from + sweep / 2));
        break;
      }
      case "dim": {
        const a = pts[0], b = pts[1], c = pts[2];
        const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.hypot(dx, dy) || 1;
        const nx = -dy / len, ny = dx / len;
        const off = (c[0] - a[0]) * nx + (c[1] - a[1]) * ny;
        const a2 = [a[0] + nx * off, a[1] + ny * off], b2 = [b[0] + nx * off, b[1] + ny * off];
        const ext = (p, q) => strokeEl3([p[0] + (q[0] - p[0]) * 0.1, p[1] + (q[1] - p[1]) * 0.1, q[0] + nx * Math.sign(off) * 4, q[1] + ny * Math.sign(off) * 4], color, "ff", Math.max(0.3, w * 0.6), "figmark", id, null, style.glow);
        out.push(ext(a, a2), ext(b, b2));
        body(flat([a2, b2]));
        out.push(strokeEl3(head(b2, a2, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow), strokeEl3(head(a2, b2, headLen), color, "ff", arrowW, "figarrow", id, null, style.glow));
        labelAt([(a2[0] + b2[0]) / 2 + nx * Math.sign(off || 1) * 10, (a2[1] + b2[1]) / 2 + ny * Math.sign(off || 1) * 10]);
        break;
      }
      default: {
        const { solid, hidden } = solidLines(s);
        for (const p of solid) body(p);
        if (s.hidden !== "none") for (const p of hidden) out.push(strokeEl3(p, color, s.hidden === "dash" ? "99" : "ff", Math.max(0.3, w * 0.8), "fighidden", id, s.hidden === "dash" ? "dash" : s.dash, style.glow));
        labelAt([P(s, 0)[0] - 14, P(s, 0)[1] + 14]);
      }
    }
    const wit = strokeEl3(witnessOf(s), color, "00", 0.3, "figwitness", id, null);
    wit.attrs.push(["xlFig", JSON.stringify({ spec: s, w: witnessOf(s), width: +w.toFixed(3) })]);
    out.push(wit);
    return out;
  }
  function figureWitness(members) {
    return members.find((m) => m.kind === "stroke" && roleOf(m) === "figwitness" && attrValue(m.attrs, "xlFig") !== null) ?? null;
  }
  function isFigure(members) {
    return figureWitness(members) !== null;
  }
  function figureSpecOf(members) {
    const wit = figureWitness(members);
    if (!wit) return null;
    let stored;
    try {
      stored = JSON.parse(attrValue(wit.attrs, "xlFig"));
    } catch {
      return null;
    }
    const spec = sanitizeFigure(stored.spec);
    const T = affineFrom(stored.w, wit.pts);
    if (!T) return null;
    const pts = mapPoints(T, spec.pts);
    const body = members.find((m) => m.kind === "stroke" && roleOf(m) === "figbody");
    const hex = (body ? attrValue(body.attrs, "color") ?? "#000000ff" : "#000000ff").slice(0, 7);
    return { spec: { ...spec, pts }, style: { color: hex, width: Number.isFinite(stored.width) && stored.width > 0 ? stored.width : 1.41, ...body?.glow ? { glow: { ...body.glow } } : {} } };
  }

  // ../../../app/src/renderer/intervals.ts
  var EPS = 1e-9;
  var same = (a, b) => a === b || Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= EPS * Math.max(1, Math.abs(a), Math.abs(b));
  var FULL = [{ lo: -Infinity, hi: Infinity, loClosed: false, hiClosed: false }];
  function piece(lo, hi, loClosed, hiClosed) {
    return { lo, hi, loClosed: loClosed && Number.isFinite(lo), hiClosed: hiClosed && Number.isFinite(hi) };
  }
  var isEmpty = (p) => p.lo > p.hi + EPS || same(p.lo, p.hi) && !(p.loClosed && p.hiClosed);
  function normalize(list) {
    const ps = list.filter((p) => !isEmpty(p)).map((p) => ({ ...p })).sort((a, b) => a.lo - b.lo || (a.loClosed === b.loClosed ? 0 : a.loClosed ? -1 : 1));
    const out = [];
    for (const p of ps) {
      const last = out[out.length - 1];
      if (last && (p.lo < last.hi - EPS || same(p.lo, last.hi) && (p.loClosed || last.hiClosed))) {
        if (p.hi > last.hi + EPS) {
          last.hi = p.hi;
          last.hiClosed = p.hiClosed;
        } else if (same(p.hi, last.hi)) last.hiClosed = last.hiClosed || p.hiClosed;
      } else out.push(p);
    }
    return out;
  }
  var union = (a, b) => normalize([...a, ...b]);
  function intersect(a, b) {
    const out = [];
    for (const p of a) {
      for (const q of b) {
        const lo = Math.max(p.lo, q.lo), hi = Math.min(p.hi, q.hi);
        const loClosed = same(p.lo, q.lo) ? p.loClosed && q.loClosed : p.lo > q.lo ? p.loClosed : q.loClosed;
        const hiClosed = same(p.hi, q.hi) ? p.hiClosed && q.hiClosed : p.hi < q.hi ? p.hiClosed : q.hiClosed;
        out.push(piece(lo, hi, loClosed, hiClosed));
      }
    }
    return normalize(out);
  }
  function complement(a) {
    const out = [];
    let lo = -Infinity, loClosed = false;
    for (const p of a) {
      out.push(piece(lo, p.lo, loClosed, !p.loClosed));
      lo = p.hi;
      loClosed = !p.hiClosed;
    }
    out.push(piece(lo, Infinity, loClosed, false));
    return normalize(out);
  }
  var difference = (a, b) => intersect(a, complement(b));
  var symmetric = (a, b) => union(difference(a, b), difference(b, a));
  function formatNumber(v) {
    if (v === Infinity) return "\u221E";
    if (v === -Infinity) return "\u2212\u221E";
    if (same(v, 0)) return "0";
    const k = v / Math.PI;
    for (const d of [1, 2, 3, 4, 6]) {
      const n = k * d;
      if (Math.abs(n) > 1e-9 && same(n, Math.round(n)) && Math.abs(Math.round(n)) <= 12 && !Number.isInteger(v)) {
        const num2 = Math.round(n);
        const top = Math.abs(num2) === 1 ? "\u03C0" : `${Math.abs(num2)}\u03C0`;
        return `${num2 < 0 ? "\u2212" : ""}${top}${d === 1 ? "" : `/${d}`}`;
      }
    }
    const s = String(+v.toFixed(6));
    return s.replace("-", "\u2212");
  }

  // ../../../app/src/renderer/setexpr.ts
  var Fail = class extends Error {
    constructor(err) {
      super(err.msg);
      this.err = err;
    }
  };
  var isLetter = (c) => !!c && /[\p{L}]/u.test(c);
  var isDigit = (c) => !!c && c >= "0" && c <= "9";
  function prepare(src) {
    return src.includes(";") ? src.replace(/(\d),(\d)/g, "$1.$2") : src;
  }
  var Parser = class {
    constructor(s) {
      this.s = s;
    }
    i = 0;
    fail(msg, pos = this.i, len = 1) {
      throw new Fail({ pos, len, msg });
    }
    ws() {
      while (this.i < this.s.length && /\s/.test(this.s[this.i])) this.i++;
    }
    peek() {
      this.ws();
      return this.s[this.i];
    }
    starts(t) {
      return this.s.startsWith(t, this.i);
    }
    eat(t) {
      this.ws();
      if (this.starts(t)) {
        this.i += t.length;
        return true;
      }
      return false;
    }
    word(w) {
      this.ws();
      const end = this.i + w.length;
      if (this.s.slice(this.i, end).toLowerCase() === w && !isLetter(this.s[end]) && !isDigit(this.s[end])) {
        this.i = end;
        return true;
      }
      return false;
    }
    // ---- numbers: 3, −2.5, 1/3, π/2, 3π/2, √2, ∞
    number() {
      this.ws();
      const start = this.i;
      let sign = 1;
      if (this.s[this.i] === "-" || this.s[this.i] === "\u2212" || this.s[this.i] === "\u2013") {
        sign = -1;
        this.i++;
      } else if (this.s[this.i] === "+") this.i++;
      this.ws();
      let v = null;
      if (this.eat("\u221E") || this.word("infinity") || this.word("inf") || this.word("oo")) v = Infinity;
      else if (this.eat("\u221A")) {
        const paren = this.eat("(");
        const n = this.plainNumber();
        if (n === null) {
          this.i = start;
          return null;
        }
        if (paren && !this.eat(")")) {
          this.i = start;
          return null;
        }
        v = Math.sqrt(n);
      } else if (this.word("sqrt")) {
        if (!this.eat("(")) {
          this.i = start;
          return null;
        }
        const n = this.plainNumber();
        if (n === null || !this.eat(")")) {
          this.i = start;
          return null;
        }
        v = Math.sqrt(n);
      } else {
        const n = this.plainNumber();
        const pi = this.eat("\u03C0") || this.word("pi");
        if (n === null && !pi) {
          this.i = start;
          return null;
        }
        v = (n ?? 1) * (pi ? Math.PI : 1);
      }
      if (v !== Infinity && this.starts("/") && (isDigit(this.s[this.i + 1]) || this.s[this.i + 1] === ".")) {
        this.i++;
        const d = this.plainNumber();
        if (d === null || d === 0) {
          this.i = start;
          return null;
        }
        v /= d;
      }
      return { v: sign * v, src: this.s.slice(start, this.i).trim() };
    }
    plainNumber() {
      const m = /^\d+(?:\.\d+)?|^\.\d+/.exec(this.s.slice(this.i));
      if (!m) return null;
      this.i += m[0].length;
      return parseFloat(m[0]);
    }
    // ---- intervals and finite sets
    interval() {
      const save = this.i;
      this.ws();
      const open = this.s[this.i];
      if (open !== "(" && open !== "[" && open !== "]") return null;
      this.i++;
      const a = this.number();
      if (!a || !(this.eat(",") || this.eat(";"))) {
        this.i = save;
        return null;
      }
      const b = this.number();
      this.ws();
      const close = this.s[this.i];
      if (!b || close !== ")" && close !== "]" && close !== "[") {
        this.i = save;
        return null;
      }
      this.i++;
      if (a.v > b.v) this.fail("El extremo izquierdo es mayor que el derecho", save, this.i - save);
      return { t: "iv", lo: { v: a.v, closed: open === "[", src: a.src }, hi: { v: b.v, closed: close === "]", src: b.src } };
    }
    finite() {
      const start = this.i;
      this.i++;
      const vals = [];
      this.ws();
      if (this.eat("}")) return { t: "fin", vals };
      for (; ; ) {
        const n = this.number();
        if (!n) this.fail("Se esperaba un n\xFAmero dentro de { }", this.i);
        vals.push(n);
        if (this.eat(",") || this.eat(";")) continue;
        if (this.eat("}")) return { t: "fin", vals };
        this.fail("Falta cerrar la llave }", start, this.i - start);
      }
    }
    // ---- operands
    atom() {
      const c = this.peek();
      if (c === void 0) this.fail("La expresi\xF3n est\xE1 incompleta", this.s.length, 0);
      if (c === "(" || c === "[" || c === "]") {
        const iv = this.interval();
        if (iv) return iv;
        if (c === "(") {
          const start = this.i;
          this.i++;
          const e = this.union();
          if (!this.eat(")")) this.fail("Falta cerrar el par\xE9ntesis )", start, 1);
          return e;
        }
        this.fail(c === "[" ? "Intervalo mal escrito: falta un extremo o el cierre" : "Intervalo mal escrito", this.i);
      }
      if (c === "{") return this.finite();
      if (c === "\u2205" || c === "\xD8") {
        this.i++;
        return { t: "fin", vals: [] };
      }
      if (c === "\xAC" || c === "~" || c === "!") {
        this.i++;
        return { t: "cp", a: this.postfix() };
      }
      if (c === "\u211D") {
        this.i++;
        return { t: "ref", name: "R" };
      }
      if (c === "\u03A9") {
        this.i++;
        return { t: "ref", name: "U" };
      }
      if (isLetter(c)) {
        const m = /^[\p{L}][\p{L}\d_]*/u.exec(this.s.slice(this.i));
        const name = /^[\p{L}](?:_?\d+)?/u.exec(m[0])[0];
        this.i += name.length;
        return { t: "ref", name: name.toUpperCase() };
      }
      const n = this.number();
      if (n) return { t: "fin", vals: [n] };
      this.fail(`No entiendo \xAB${c}\xBB`, this.i);
    }
    postfix() {
      let e = this.atom();
      for (; ; ) {
        this.ws();
        if (this.eat("'") || this.eat("\u2019") || this.eat("\u2032") || this.eat("\u1D9C") || this.eat("^c") || this.eat("^C") || this.eat("^{c}") || this.eat("^{C}")) e = { t: "cp", a: e };
        else return e;
      }
    }
    /** Intersection: ∩, &, *, n, and. */
    interOp() {
      this.ws();
      if (this.eat("\u2229") || this.eat("&") || this.eat("*")) return true;
      if (this.word("and")) return true;
      if (this.s[this.i] === "n" && !isLetter(this.s[this.i + 1]) && !isDigit(this.s[this.i + 1])) {
        this.i++;
        return true;
      }
      return false;
    }
    inter() {
      let e = this.postfix();
      while (this.interOp()) e = { t: "in", a: e, b: this.postfix() };
      return e;
    }
    /** Union, difference and symmetric difference, at the same level. */
    union() {
      let e = this.inter();
      for (; ; ) {
        this.ws();
        let kind = null;
        if (this.eat("\u222A") || this.eat("|") || this.eat("+")) kind = "un";
        else if (this.eat("\u2216") || this.eat("\\") || this.eat("\u2212") || this.eat("\u2013")) kind = "df";
        else if (this.eat("-")) kind = "df";
        else if (this.eat("\u0394") || this.eat("\u25B3") || this.eat("\u25B5")) kind = "sd";
        else if (this.word("or")) kind = "un";
        else if (this.word("minus")) kind = "df";
        else if (this.word("xor")) kind = "sd";
        else if ((this.s[this.i] === "U" || this.s[this.i] === "u") && !isLetter(this.s[this.i + 1]) && !isDigit(this.s[this.i + 1]) && this.s[this.i + 1] !== "_") {
          this.i++;
          kind = "un";
        }
        if (!kind) return e;
        e = { t: kind, a: e, b: this.inter() };
      }
    }
  };
  function parseSetExpr(src) {
    const text3 = prepare(src);
    const p = new Parser(text3);
    try {
      p.ws();
      if (p.i >= text3.length) return { ok: false, pos: 0, len: 0, msg: "Escribe una expresi\xF3n" };
      const expr = p.union();
      p.ws();
      if (p.i < text3.length) throw new Fail({ pos: p.i, len: text3.length - p.i, msg: `Sobra \xAB${text3.slice(p.i, p.i + 8).trim()}\xBB: falta un operador (\u222A \u2229 \u2216 \u0394) entre las partes` });
      return { ok: true, expr };
    } catch (e) {
      if (e instanceof Fail) return { ok: false, ...e.err };
      throw e;
    }
  }
  function evalReal(e) {
    try {
      return { ok: true, value: real(e) };
    } catch (err) {
      if (err instanceof Fail) return { ok: false, msg: err.err.msg };
      throw err;
    }
  }
  function real(e) {
    switch (e.t) {
      case "iv":
        return normalize([piece(e.lo.v, e.hi.v, e.lo.closed, e.hi.closed)]);
      case "fin":
        return normalize(e.vals.map((x) => piece(x.v, x.v, true, true)));
      case "ref":
        if (e.name === "R" || e.name === "U") return FULL;
        throw new Fail({ pos: 0, len: 0, msg: `\xAB${e.name}\xBB no es un conjunto de n\xFAmeros: en la recta usa intervalos, {\u2026}, R o \u2205` });
      case "un":
        return union(real(e.a), real(e.b));
      case "in":
        return intersect(real(e.a), real(e.b));
      case "df":
        return difference(real(e.a), real(e.b));
      case "sd":
        return symmetric(real(e.a), real(e.b));
      case "cp":
        return complement(real(e.a));
    }
  }
  function evalRegions(e, names) {
    try {
      const k = names.length;
      const out = [];
      for (let m = 0; m < 1 << k; m++) out.push(inRegion(e, names, m));
      return { ok: true, value: out };
    } catch (err) {
      if (err instanceof Fail) return { ok: false, msg: err.err.msg };
      throw err;
    }
  }
  function inRegion(e, names, m) {
    switch (e.t) {
      case "ref": {
        if (e.name === "U") return true;
        const i = names.findIndex((n) => n.toUpperCase() === e.name);
        if (i < 0) throw new Fail({ pos: 0, len: 0, msg: `No hay un conjunto \xAB${e.name}\xBB en el diagrama (hay ${names.join(", ")})` });
        return (m >> i & 1) === 1;
      }
      case "iv":
      case "fin":
        throw new Fail({ pos: 0, len: 0, msg: "Un diagrama de conjuntos no lleva intervalos ni n\xFAmeros: usa los nombres de los conjuntos" });
      case "un":
        return inRegion(e.a, names, m) || inRegion(e.b, names, m);
      case "in":
        return inRegion(e.a, names, m) && inRegion(e.b, names, m);
      case "df":
        return inRegion(e.a, names, m) && !inRegion(e.b, names, m);
      case "sd":
        return inRegion(e.a, names, m) !== inRegion(e.b, names, m);
      case "cp":
        return !inRegion(e.a, names, m);
    }
  }

  // ../../../app/src/renderer/numberline.ts
  var SET_ATTR = "xlSet";
  var MAX_ROWS = 3;
  var MAX_TICKS = 120;
  var PAD = 14;
  var ROW_GAP = 40;
  var defaultLine = (a = [100, 200], b = [500, 200]) => ({
    kind: "line",
    pts: [a[0], a[1], b[0], b[1]],
    min: -5,
    max: 5,
    step: 1,
    every: 1,
    ticks: true,
    numbers: true,
    arrows: true,
    marks: "circles",
    rows: [{ expr: "", label: "" }]
  });
  var num = (v, d, lo = -1e9, hi = 1e9) => typeof v === "number" && Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : d;
  function sanitizeLine(raw) {
    const o = typeof raw === "object" && raw !== null ? raw : {};
    const d = defaultLine();
    const pts = Array.isArray(o.pts) && o.pts.length >= 4 ? o.pts.slice(0, 4).map((v, i) => num(v, d.pts[i], -1e6, 1e6)) : d.pts;
    let min = num(o.min, d.min), max = num(o.max, d.max);
    if (!(max > min)) max = min + 1;
    const rowsIn = Array.isArray(o.rows) ? o.rows.slice(0, MAX_ROWS) : [];
    const rows = rowsIn.map((r) => {
      const q = typeof r === "object" && r !== null ? r : {};
      return { expr: typeof q.expr === "string" ? q.expr.slice(0, 200) : "", label: typeof q.label === "string" ? q.label.slice(0, 40) : "" };
    });
    if (!rows.length) rows.push({ expr: "", label: "" });
    return {
      kind: "line",
      pts,
      min,
      max,
      step: Math.max((max - min) / 400, num(o.step, d.step, 1e-6, 1e9)),
      every: Math.round(num(o.every, 1, 1, 50)),
      ticks: o.ticks !== false,
      numbers: o.numbers !== false,
      arrows: o.arrows !== false,
      marks: o.marks === "brackets" ? "brackets" : "circles",
      rows
    };
  }
  function lineSets(spec) {
    return spec.rows.map((r) => {
      if (!r.expr.trim()) return { set: null, error: null };
      const p = parseSetExpr(r.expr);
      if (!p.ok) return { set: null, error: p.msg };
      const e = evalReal(p.expr);
      return e.ok ? { set: e.value, error: null } : { set: null, error: e.msg };
    });
  }
  function frameOf(spec) {
    const [x0, y0, x1, y1] = spec.pts;
    const len = Math.hypot(x1 - x0, y1 - y0) || 1;
    const u = [(x1 - x0) / len, (y1 - y0) / len];
    return { p0: [x0, y0], u, up: [u[1], -u[0]], len, scale: len / (spec.max - spec.min) };
  }
  var at = (f, s, off = 0) => [f.p0[0] + f.u[0] * s + f.up[0] * off, f.p0[1] + f.u[1] * s + f.up[1] * off];
  function without(a, b, gaps) {
    let out = [[a, b]];
    for (const [g0, g1] of gaps) {
      out = out.flatMap(([s, e]) => {
        if (g1 <= s || g0 >= e) return [[s, e]];
        const r = [];
        if (g0 > s) r.push([s, g0]);
        if (g1 < e) r.push([g1, e]);
        return r;
      });
    }
    return out.filter(([s, e]) => e - s > 0.05);
  }
  function markersOf(set, min, max) {
    const byValue = /* @__PURE__ */ new Map();
    const add = (v, closed, side) => {
      if (!Number.isFinite(v) || v < min - 1e-9 || v > max + 1e-9) return;
      const k = v.toFixed(9);
      const old = byValue.get(k);
      if (!old) byValue.set(k, { v, closed, side });
      else byValue.set(k, { v, closed: old.closed || closed, side: old.side });
    };
    for (const p of set) {
      if (p.lo === p.hi) add(p.lo, true, 1);
      else {
        add(p.lo, p.loClosed, 1);
        add(p.hi, p.hiClosed, -1);
      }
    }
    return [...byValue.values()].sort((a, b) => a.v - b.v);
  }
  function circle(c, r) {
    const out = [];
    for (let i = 0; i <= 24; i++) out.push(c[0] + Math.cos(i / 24 * Math.PI * 2) * r, c[1] + Math.sin(i / 24 * Math.PI * 2) * r);
    return out;
  }
  function buildLine(id, rawSpec, style) {
    const spec = sanitizeLine(rawSpec);
    const f = frameOf(spec);
    const w = Math.max(0.3, style.width), color = style.color;
    const hiW = w * 2.6;
    const R = Math.min(7, Math.max(3.6, 3 + w * 1.6));
    const out = [];
    const line = (pts, role, width = w) => out.push(strokeEl3(pts, color, "ff", width, role, id, null, style.glow));
    const sets = lineSets(spec);
    let step = spec.step;
    while ((spec.max - spec.min) / step > MAX_TICKS) step *= 2;
    const k0 = Math.ceil(spec.min / step - 1e-9), k1 = Math.floor(spec.max / step + 1e-9);
    spec.rows.forEach((row, ri) => {
      const off = ri * ROW_GAP;
      const set = sets[ri]?.set ?? null;
      const circles = spec.marks === "circles";
      const markers = set ? markersOf(set, spec.min, spec.max) : [];
      const gaps = circles ? markers.filter((m) => !m.closed).map((m) => {
        const s = (m.v - spec.min) * f.scale;
        return [s - R, s + R];
      }) : [];
      for (const [s0, s1] of without(-PAD, f.len + PAD, gaps)) line([...at(f, s0, off), ...at(f, s1, off)], "setbody", ri === 0 ? w : w * 0.7);
      if (spec.arrows) {
        line(head(at(f, f.len + PAD, off), at(f, 0, off), 8), "setarrow", w);
        line(head(at(f, -PAD, off), at(f, f.len, off), 8), "setarrow", w);
      }
      if (spec.ticks) {
        const tl = ri === 0 ? 5 : 3;
        for (let k = k0; k <= k1; k++) {
          const s = (k * step - spec.min) * f.scale;
          if (gaps.some(([g0, g1]) => s > g0 && s < g1)) continue;
          line([...at(f, s, off + tl), ...at(f, s, off - tl)], "setmark", w * 0.8);
          if (ri === 0 && spec.numbers && (k - k0) % spec.every === 0) out.push(textEl3(formatNumber(k * step), at(f, s, off - tl - 9), 10, color, id, "setlabel"));
        }
      }
      if (set) {
        for (const p of set) drawPiece(p);
        for (const m of markers) drawMarker(m);
      }
      if (row.label) out.push(textEl3(row.label, at(f, -PAD - 4, off + 16), 12, color, id, "setlabel"));
      function drawPiece(p) {
        if (p.hi < spec.min - 1e-9 || p.lo > spec.max + 1e-9 || p.lo === p.hi) return;
        const leftOpenEnd = p.lo < spec.min - 1e-9, rightOpenEnd = p.hi > spec.max + 1e-9;
        const s0 = leftOpenEnd ? -PAD : (p.lo - spec.min) * f.scale, s1 = rightOpenEnd ? f.len + PAD : (p.hi - spec.min) * f.scale;
        const trim = (m, closed) => circles && m && !closed ? R : 0;
        const a = s0 + trim(!leftOpenEnd, p.loClosed), b = s1 - trim(!rightOpenEnd, p.hiClosed);
        if (b > a) line([...at(f, a, off), ...at(f, b, off)], "setbody", hiW);
        if (!spec.arrows) {
          if (rightOpenEnd) line(head(at(f, f.len + PAD, off), at(f, f.len - 30, off), 9), "setarrow", w);
          if (leftOpenEnd) line(head(at(f, -PAD, off), at(f, 30, off), 9), "setarrow", w);
        }
      }
      function drawMarker(m) {
        const s = (m.v - spec.min) * f.scale, c = at(f, s, off);
        if (circles) {
          if (m.closed) line([c[0], c[1], c[0] + 0.01, c[1]], "setbody", R * 2);
          else line(circle(c, R), "setbody", w);
          return;
        }
        const d = [f.u[0] * m.side, f.u[1] * m.side];
        const H = 9, hook = 5;
        if (m.closed) {
          const top = at(f, s, off + H), bot = at(f, s, off - H);
          line([top[0] + d[0] * hook, top[1] + d[1] * hook, ...top, ...bot, bot[0] + d[0] * hook, bot[1] + d[1] * hook], "setbody", w * 1.3);
        } else {
          const top = at(f, s, off + H), bot = at(f, s, off - H), ctl = [c[0] - d[0] * 7, c[1] - d[1] * 7];
          const pts = [];
          for (let i = 0; i <= 10; i++) {
            const t = i / 10, q = 1 - t;
            pts.push(q * q * top[0] + 2 * q * t * ctl[0] + t * t * bot[0], q * q * top[1] + 2 * q * t * ctl[1] + t * t * bot[1]);
          }
          line(pts, "setbody", w * 1.3);
        }
      }
    });
    out.push(makeWitness(witnessOf2(spec), "setwitness", SET_ATTR, { kind: "line", spec, w: witnessOf2(spec), width: +w.toFixed(3) }, color, id));
    return out;
  }
  function witnessOf2(spec) {
    const f = frameOf(spec);
    const p = at(f, 0, 30);
    return [...spec.pts, p[0], p[1]];
  }
  function lineAnchors(spec) {
    return [[spec.pts[0], spec.pts[1]], [spec.pts[2], spec.pts[3]]];
  }
  function readLine(members) {
    const wit = findWitness(members, "setwitness", SET_ATTR, roleOf);
    if (!wit) return null;
    let stored;
    try {
      stored = JSON.parse(attrValue(wit.attrs, SET_ATTR));
    } catch {
      return null;
    }
    if (stored.kind !== "line") return null;
    const spec = sanitizeLine(stored.spec);
    const T = affineFrom(stored.w, wit.pts);
    if (!T) return null;
    spec.pts = mapPoints(T, spec.pts);
    const body = members.find((m) => m.kind === "stroke" && roleOf(m) === "setbody");
    const hex = (body ? attrValue(body.attrs, "color") ?? "#000000ff" : "#000000ff").slice(0, 7);
    return { spec, style: { color: hex, width: Number.isFinite(stored.width) && stored.width > 0 ? stored.width : 1.41, ...body?.glow ? { glow: { ...body.glow } } : {} } };
  }

  // ../../../app/src/renderer/venn.ts
  var MAX_LINES3 = 400;
  function defaultVenn(k = 2, a = [100, 100], b = [500, 360]) {
    const w = b[0] - a[0], h = b[1] - a[1];
    const s = Math.min(w * 0.5, h * (k === 2 ? 0.8 : 0.62));
    const r = k === 2 ? s * 0.5 : s * 0.5;
    const mid = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    const centres = k === 2 ? [[mid[0] - r * 0.62, mid[1]], [mid[0] + r * 0.62, mid[1]]] : [[mid[0] - r * 0.6, mid[1] - r * 0.4], [mid[0] + r * 0.6, mid[1] - r * 0.4], [mid[0], mid[1] + r * 0.62]];
    const pts = [a[0], a[1], b[0], b[1]];
    for (const c of centres) {
      const d = Math.hypot(c[0] - mid[0], c[1] - mid[1]) || 1;
      const dir = d < 1e-6 ? [0.7071, -0.7071] : [(c[0] - mid[0]) / d, (c[1] - mid[1]) / d];
      pts.push(c[0], c[1], c[0] + dir[0] * r, c[1] + dir[1] * r);
    }
    return { kind: "venn", pts, k, names: ["A", "B", "C"], expr: "", universe: true, hatch: "light", lists: ["", "", "", ""].slice(0, k + 1) };
  }
  function sanitizeVenn(raw) {
    const o = typeof raw === "object" && raw !== null ? raw : {};
    const k = o.k === 3 ? 3 : 2;
    const d = defaultVenn(k);
    const need = 4 + 4 * k;
    const pts = Array.isArray(o.pts) && o.pts.length >= need && o.pts.slice(0, need).every((v) => typeof v === "number" && Number.isFinite(v)) ? o.pts.slice(0, need) : d.pts;
    const names = Array.isArray(o.names) ? o.names.slice(0, 3).map((n, i) => typeof n === "string" && /^[\p{L}][\p{L}\d_]{0,3}$/u.test(n.trim()) ? n.trim() : d.names[i]) : d.names;
    while (names.length < 3) names.push(d.names[names.length]);
    const lists = Array.isArray(o.lists) ? o.lists.slice(0, k + 1).map((l) => typeof l === "string" ? l.slice(0, 300) : "") : [];
    while (lists.length < k + 1) lists.push("");
    return { kind: "venn", pts, k, names, expr: typeof o.expr === "string" ? o.expr.slice(0, 200) : "", universe: o.universe !== false, hatch: o.hatch === "dense" ? "dense" : "light", lists };
  }
  function circlesOf(spec) {
    const out = [];
    for (let i = 0; i < spec.k; i++) {
      const cx = spec.pts[4 + 4 * i], cy = spec.pts[5 + 4 * i], rx = spec.pts[6 + 4 * i], ry = spec.pts[7 + 4 * i];
      out.push({ c: [cx, cy], r: Math.max(8, Math.hypot(rx - cx, ry - cy)) });
    }
    return out;
  }
  function rectOf(spec) {
    const [ax, ay, bx, by] = spec.pts;
    return [Math.min(ax, bx), Math.min(ay, by), Math.max(ax, bx), Math.max(ay, by)];
  }
  function maskAt(spec, p) {
    let m = 0;
    circlesOf(spec).forEach((ci, i) => {
      if (Math.hypot(p[0] - ci.c[0], p[1] - ci.c[1]) < ci.r) m |= 1 << i;
    });
    return m;
  }
  function shadedRegions(spec) {
    if (!spec.expr.trim()) return { shaded: null, error: null };
    const p = parseSetExpr(spec.expr);
    if (!p.ok) return { shaded: null, error: p.msg };
    const e = evalRegions(p.expr, spec.names.slice(0, spec.k));
    return e.ok ? { shaded: e.value, error: null } : { shaded: null, error: e.msg };
  }
  var splitList = (s) => s.split(/[,;\n]/).map((x) => x.trim()).filter(Boolean);
  function elementsOf(spec) {
    const masks = /* @__PURE__ */ new Map();
    for (let i = 0; i < spec.k; i++) for (const e of splitList(spec.lists[i] ?? "")) masks.set(e, (masks.get(e) ?? 0) | 1 << i);
    for (const e of splitList(spec.lists[spec.k] ?? "")) if (!masks.has(e)) masks.set(e, 0);
    return [...masks].map(([label, mask]) => ({ label, mask }));
  }
  function chord(base, v, c) {
    const bx = base[0] - c.c[0], by = base[1] - c.c[1];
    const b = bx * v[0] + by * v[1], cc = bx * bx + by * by - c.r * c.r, disc = b * b - cc;
    return disc > 0 ? normalize([piece(-b - Math.sqrt(disc), -b + Math.sqrt(disc), true, true)]) : [];
  }
  function hatchSegments(spec, shaded) {
    const [x0, y0, x1, y1] = rectOf(spec);
    const circles = circlesOf(spec);
    const gap = spec.hatch === "dense" ? 5 : 9;
    let d = gap;
    if ((x1 - x0 + (y1 - y0)) / d > MAX_LINES3) d = (x1 - x0 + (y1 - y0)) / MAX_LINES3;
    const v = [Math.SQRT1_2, -Math.SQRT1_2];
    const out = [];
    const masks = shaded.map((s, m) => s ? m : -1).filter((m) => m >= 0);
    if (!masks.length) return out;
    const cMin = x0 + y0;
    for (let c = cMin + d / 2; c < x1 + y1; c += d) {
      const base = [c / 2, c / 2];
      let tLo = -Infinity, tHi = Infinity;
      for (const [axis, lo, hi] of [[0, x0, x1], [1, y0, y1]]) {
        const bv = base[axis], vv = v[axis];
        const a = (lo - bv) / vv, b = (hi - bv) / vv;
        tLo = Math.max(tLo, Math.min(a, b));
        tHi = Math.min(tHi, Math.max(a, b));
      }
      if (!(tHi > tLo)) continue;
      const universe = normalize([piece(tLo, tHi, true, true)]);
      const inside = circles.map((ci) => chord(base, v, ci));
      let total = [];
      for (const m of masks) {
        let part = universe;
        circles.forEach((_, i) => {
          part = intersect(part, (m >> i & 1) === 1 ? inside[i] : complement(inside[i]));
        });
        total = union(total, part);
      }
      for (const p of total) if (p.hi - p.lo > 0.5) out.push([base[0] + v[0] * p.lo, base[1] + v[1] * p.lo, base[0] + v[0] * p.hi, base[1] + v[1] * p.hi]);
    }
    return out;
  }
  function placeElements(spec) {
    const els = elementsOf(spec);
    const [x0, y0, x1, y1] = rectOf(spec);
    const circles = circlesOf(spec);
    const step = Math.max(3, Math.min(x1 - x0, y1 - y0) / 60);
    const dist = (p) => {
      let d = Math.min(p[0] - x0, x1 - p[0], p[1] - y0, y1 - p[1]);
      for (const ci of circles) d = Math.min(d, Math.abs(Math.hypot(p[0] - ci.c[0], p[1] - ci.c[1]) - ci.r));
      return d;
    };
    const samples = /* @__PURE__ */ new Map();
    for (let x = x0 + step / 2; x < x1; x += step) for (let y = y0 + step / 2; y < y1; y += step) {
      const p = [x, y], m = maskAt(spec, p);
      const list = samples.get(m) ?? [];
      list.push({ p, d: dist(p) });
      samples.set(m, list);
    }
    const placed = /* @__PURE__ */ new Map();
    const out = [];
    for (const e of els) {
      const cands = samples.get(e.mask) ?? [];
      const mine = placed.get(e.mask) ?? [];
      let best = null, bestScore = -1;
      for (const s of cands) {
        const near = mine.length ? Math.min(...mine.map((q) => Math.hypot(q[0] - s.p[0], q[1] - s.p[1]))) : Infinity;
        const score = Math.min(s.d, near * 0.55);
        if (score > bestScore) {
          bestScore = score;
          best = s.p;
        }
      }
      if (!best) best = [(x0 + x1) / 2, (y0 + y1) / 2];
      mine.push(best);
      placed.set(e.mask, mine);
      out.push({ label: e.label, at: best });
    }
    return out;
  }
  function polygon(c, r) {
    const out = [];
    const n = Math.max(36, Math.min(120, Math.round(r / 2)));
    for (let i = 0; i <= n; i++) out.push(c[0] + Math.cos(i / n * Math.PI * 2) * r, c[1] + Math.sin(i / n * Math.PI * 2) * r);
    return out;
  }
  function buildVenn(id, rawSpec, style) {
    const spec = sanitizeVenn(rawSpec);
    const w = Math.max(0.3, style.width), color = style.color;
    const out = [];
    const [x0, y0, x1, y1] = rectOf(spec);
    const circles = circlesOf(spec);
    const line = (pts, role, width = w, alpha2 = "ff") => out.push(strokeEl3(pts, color, alpha2, width, role, id, null, style.glow));
    const { shaded } = shadedRegions(spec);
    if (shaded) for (const seg of hatchSegments(spec, shaded)) line(seg, "sethatch", Math.max(0.5, w * 0.5), "b0");
    if (spec.universe) {
      line([x0, y0, x1, y0, x1, y1, x0, y1, x0, y0], "setbody");
      out.push(textEl3("U", [x0 + 12, y0 + 12], 13, color, id, "setlabel"));
    }
    const mid = [(x0 + x1) / 2, (y0 + y1) / 2];
    circles.forEach((ci, i) => {
      line(polygon(ci.c, ci.r), "setbody");
      const dx = ci.c[0] - mid[0], dy = ci.c[1] - mid[1], d = Math.hypot(dx, dy);
      const dir = d < 1e-6 ? [0, -1] : [dx / d, dy / d];
      out.push(textEl3(spec.names[i], [ci.c[0] + dir[0] * (ci.r + 12), ci.c[1] + dir[1] * (ci.r + 12)], 14, color, id, "setlabel"));
    });
    for (const e of placeElements(spec)) {
      line([e.at[0], e.at[1], e.at[0] + 0.01, e.at[1]], "setmark", 3.2);
      out.push(textEl3(e.label, [e.at[0] + 5 + e.label.length * 3, e.at[1]], 11, color, id, "setlabel"));
    }
    const w3 = witnessOf3(spec);
    out.push(makeWitness(w3, "setwitness", SET_ATTR, { kind: "venn", spec, w: w3, width: +w.toFixed(3) }, color, id));
    return out;
  }
  function witnessOf3(spec) {
    const [x0, y0, x1, y1] = rectOf(spec);
    return [x0, y0, x1, y0, x0, y1];
  }
  function vennAnchors(spec) {
    const out = [];
    for (let i = 0; i + 1 < 4 + 4 * spec.k; i += 2) out.push([spec.pts[i], spec.pts[i + 1]]);
    return out;
  }
  function readVenn(members) {
    const wit = findWitness(members, "setwitness", SET_ATTR, roleOf);
    if (!wit) return null;
    let stored;
    try {
      stored = JSON.parse(attrValue(wit.attrs, SET_ATTR));
    } catch {
      return null;
    }
    if (stored.kind !== "venn") return null;
    const spec = sanitizeVenn(stored.spec);
    const T = affineFrom(stored.w, wit.pts);
    if (!T) return null;
    spec.pts = mapPoints(T, spec.pts);
    const body = members.find((m) => m.kind === "stroke" && roleOf(m) === "setbody");
    const hex = (body ? attrValue(body.attrs, "color") ?? "#000000ff" : "#000000ff").slice(0, 7);
    return { spec, style: { color: hex, width: Number.isFinite(stored.width) && stored.width > 0 ? stored.width : 1.41, ...body?.glow ? { glow: { ...body.glow } } : {} } };
  }

  // ../../../app/src/renderer/mathobj.ts
  function isMath(members) {
    return members.some((m) => m.kind === "stroke" && roleOf(m) === "setwitness" && attrValue(m.attrs, SET_ATTR) !== null);
  }
  function readMath(members) {
    return readLine(members) ?? readVenn(members);
  }
  function buildMath(id, spec, style) {
    return spec.kind === "line" ? buildLine(id, spec, style) : buildVenn(id, spec, style);
  }
  var sanitizeMath = (spec) => spec.kind === "line" ? sanitizeLine(spec) : sanitizeVenn(spec);
  var mathAnchors = (spec) => spec.kind === "line" ? lineAnchors(spec) : vennAnchors(spec);

  // ../../../app/src/renderer/repair.ts
  var lacksPlace = (e) => e.kind === "text" && attrValue(e.attrs, "x") === null && attrValue(e.attrs, "y") === null;
  function repairGenerated(doc) {
    let fixed = 0;
    for (const page2 of doc.pages) {
      for (const layer2 of page2.layers) fixed += repairLayer(doc, layer2);
    }
    return fixed;
  }
  function repairLayer(doc, layer2) {
    const broken = /* @__PURE__ */ new Set();
    for (const e of layer2.elements) {
      const id = planeIdOf(e);
      if (id && lacksPlace(e)) broken.add(id);
    }
    let fixed = 0;
    for (const id of broken) {
      const members = layer2.elements.filter((e) => planeIdOf(e) === id);
      let fresh = null;
      const spec = planeSpecOf(members), frame = planeFrameOf(members), style = planeStyleOf(members);
      if (spec && frame && style) fresh = buildPlane(id, spec, frame, style);
      else if (isSpace3(members)) {
        const s3 = space3SpecOf(members), st = space3StyleOf(members);
        if (s3 && st) fresh = buildSpace3(id, s3, st);
      } else if (isFigure(members)) {
        const f = figureSpecOf(members);
        if (f) fresh = buildFigure(id, f.spec, f.style);
      }
      if (!fresh) continue;
      const at2 = layer2.elements.findIndex((e) => planeIdOf(e) === id);
      layer2.elements = layer2.elements.filter((e) => planeIdOf(e) !== id);
      layer2.elements.splice(at2, 0, ...fresh);
      doc.elementCount += fresh.length - members.length;
      fixed++;
    }
    return fixed;
  }

  // math_entry.ts
  function kindAndPts(m) {
    if (isSpace3(m)) {
      const s = space3SpecOf(m);
      if (!s) return { kind: "space3?", pts: [] };
      return { kind: "space3", pts: [s.o[0], s.o[1], ...anchorsOf(s).flatMap((a) => [a.at[0], a.at[1]])] };
    }
    if (isFigure(m)) {
      const r = figureSpecOf(m);
      return { kind: r ? "figure" : "figure?", pts: r ? r.spec.pts : [] };
    }
    if (isMath(m)) {
      const r = readMath(m);
      return r ? { kind: r.spec.kind, pts: mathAnchors(r.spec).flatMap((p) => [p[0], p[1]]) } : { kind: "math?", pts: [] };
    }
    const spec = planeSpecOf(m), f = planeFrameOf(m), st = planeStyleOf(m);
    if (spec && f && st) return { kind: "plane", pts: [f.x0, f.y0, f.x1, f.y0, f.x1, f.y1, f.x0, f.y1] };
    return { kind: "none", pts: [] };
  }
  function probeDoc(doc) {
    const out = [];
    doc.pages.forEach((p, pi) => {
      for (const layer2 of p.layers) {
        const ids = [];
        for (const e of layer2.elements) {
          const id = planeIdOf(e);
          if (id && !ids.includes(id)) ids.push(id);
        }
        for (const id of ids) {
          const m = layer2.elements.filter((e) => planeIdOf(e) === id);
          const kp = kindAndPts(m);
          const texts = m.filter((e) => e.kind === "text");
          out.push({ page: pi, id, ...kp, pieces: m.length, texts: texts.length, textsWithoutPlace: texts.filter((t) => attrValue(t.attrs, "x") === null && attrValue(t.attrs, "y") === null).length });
        }
      }
    });
    return out;
  }
  function probe(xml) {
    return probeDoc(parseXopp(xml));
  }
  function repairProbe(xml) {
    const doc = parseXopp(xml);
    for (const p of doc.pages) for (const l of p.layers) {
      const kinds = /* @__PURE__ */ new Map();
      for (const e of l.elements) {
        const id = planeIdOf(e);
        if (id && !kinds.has(id)) kinds.set(id, kindAndPts(l.elements.filter((x) => planeIdOf(x) === id)).kind);
      }
      for (const e of l.elements) {
        const id = planeIdOf(e);
        if (e.kind === "text" && id && ["plane", "space3", "figure"].includes(kinds.get(id) ?? "")) e.attrs = e.attrs.filter(([k]) => k !== "x" && k !== "y" && k !== "font" && k !== "size" && k !== "color");
      }
    }
    const before = probeDoc(doc);
    const repaired = repairGenerated(doc);
    return { repaired, before, after: probeDoc(doc), againZero: repairGenerated(doc) === 0 };
  }
  var TEMPLATE = `<?xml version="1.0" standalone="no"?><xournal creator="xl-interchange" fileversion="4"><title>Xournal++ document - see https://github.com/xournalpp/xournalpp</title><page width="595.27559100" height="841.88976400"><background type="solid" color="#ffffffff" style="plain"/><layer></layer></page></xournal>`;
  function make() {
    const doc = parseXopp(TEMPLATE);
    const style = { color: "#0044cc", width: 1.41 };
    const objs = [];
    objs.push({ name: "plane", build: (id) => buildPlane(id, DEFAULT_PLANE, { x0: 150, y0: 150, x1: 450, y1: 400 }, style) });
    objs.push({ name: "space3", build: (id) => buildSpace3(id, placedSpec(DEFAULT_SPACE3, 1, [150, 160]), style) });
    for (const f of FIGURES) objs.push({ name: "figure " + f.kind, build: (id) => buildFigure(id, sanitizeFigure({ ...defaultSpec2(f.kind, [160, 170], [420, 360]), label: f.kind === "segment" ? "AB" : "" }), style) });
    objs.push({ name: "line", build: (id) => buildMath(id, sanitizeMath({ ...defaultLine([150, 250], [450, 250]), rows: [{ expr: "(-2, 3] U {4}", label: "A" }, { expr: "[1, inf)", label: "B" }] }), style) });
    objs.push({ name: "venn2", build: (id) => buildMath(id, sanitizeMath({ ...defaultVenn(2, [150, 150], [450, 360]), expr: "A \u2229 B", lists: ["1, 2", "2, 3", "", "9"] }), style) });
    objs.push({ name: "venn3", build: (id) => buildMath(id, sanitizeMath({ ...defaultVenn(3, [150, 150], [450, 400]), expr: "(A \u222A B)'" }), style) });
    const tmpl = doc.pages[0];
    doc.pages = [];
    objs.forEach((o, i) => {
      const page2 = JSON.parse(JSON.stringify(tmpl));
      page2.layers[0].elements = o.build("plE" + String(i).padStart(3, "0"));
      doc.pages.push(page2);
    });
    const xml = serializeXopp(doc);
    return { xml, probe: probeDoc(parseXopp(xml)).map((p) => ({ ...p, id: objs[p.page].name })) };
  }
  window.xlm = { probe, repairProbe, make };
})();
