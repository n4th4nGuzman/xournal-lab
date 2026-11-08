/**
 * A small language for sets, written the way it is on a blackboard: intervals ((−2, 3], [1, ∞), ]a, b[), finite sets ({1, 2, 5}, ∅),
 * names of sets (A, B, C, U for the universe, R for the real line) and the operations between them: union, intersection, difference,
 * symmetric difference and complement. It is forgiving about how things are typed (U, u, | or ∪; n, & or ∩; inf, oo or ∞; A', A^c or Aᶜ)
 * because it is written with a pen tablet more often than with a keyboard. The text the user typed is what is kept; this module only
 * turns it into a tree, and the tree into a set of numbers or into the regions of a diagram.
 */
import { EMPTY, FULL, complement, difference, intersect, normalize, piece, symmetric, union, type IntervalSet, type Piece } from "./intervals";

export interface Bound { v: number; closed: boolean; src: string }
export type Expr =
  | { t: "iv"; lo: Bound; hi: Bound }
  | { t: "fin"; vals: Array<{ v: number; src: string }> }
  | { t: "ref"; name: string }
  | { t: "un" | "in" | "df" | "sd"; a: Expr; b: Expr }
  | { t: "cp"; a: Expr };

export interface SetError { pos: number; len: number; msg: string }
export type Parsed = { ok: true; expr: Expr } | ({ ok: false } & SetError);

class Fail extends Error { constructor(readonly err: SetError) { super(err.msg); } }

const isLetter = (c: string | undefined) => !!c && /[\p{L}]/u.test(c);
const isDigit = (c: string | undefined) => !!c && c >= "0" && c <= "9";

/** `(1,5; 3]` writes decimals with a comma: when ";" separates the ends, a comma between digits is a decimal point. */
function prepare(src: string): string {
  return src.includes(";") ? src.replace(/(\d),(\d)/g, "$1.$2") : src;
}

class Parser {
  i = 0;
  constructor(readonly s: string) {}

  fail(msg: string, pos = this.i, len = 1): never { throw new Fail({ pos, len, msg }); }
  ws(): void { while (this.i < this.s.length && /\s/.test(this.s[this.i]!)) this.i++; }
  peek(): string | undefined { this.ws(); return this.s[this.i]; }
  starts(t: string): boolean { return this.s.startsWith(t, this.i); }
  eat(t: string): boolean { this.ws(); if (this.starts(t)) { this.i += t.length; return true; } return false; }
  word(w: string): boolean { this.ws(); const end = this.i + w.length; if (this.s.slice(this.i, end).toLowerCase() === w && !isLetter(this.s[end]) && !isDigit(this.s[end])) { this.i = end; return true; } return false; }

  // ---- numbers: 3, −2.5, 1/3, π/2, 3π/2, √2, ∞
  number(): { v: number; src: string } | null {
    this.ws();
    const start = this.i;
    let sign = 1;
    if (this.s[this.i] === "-" || this.s[this.i] === "−" || this.s[this.i] === "–") { sign = -1; this.i++; } else if (this.s[this.i] === "+") this.i++;
    this.ws();
    let v: number | null = null;
    if (this.eat("∞") || this.word("infinity") || this.word("inf") || this.word("oo")) v = Infinity;
    else if (this.eat("√")) {
      const paren = this.eat("(");
      const n = this.plainNumber();
      if (n === null) { this.i = start; return null; }
      if (paren && !this.eat(")")) { this.i = start; return null; }
      v = Math.sqrt(n);
    } else if (this.word("sqrt")) {
      if (!this.eat("(")) { this.i = start; return null; }
      const n = this.plainNumber();
      if (n === null || !this.eat(")")) { this.i = start; return null; }
      v = Math.sqrt(n);
    } else {
      const n = this.plainNumber();
      const pi = this.eat("π") || this.word("pi");
      if (n === null && !pi) { this.i = start; return null; }
      v = (n ?? 1) * (pi ? Math.PI : 1);
    }
    if (v !== Infinity && this.starts("/") && (isDigit(this.s[this.i + 1]) || this.s[this.i + 1] === ".")) {
      this.i++;
      const d = this.plainNumber();
      if (d === null || d === 0) { this.i = start; return null; }
      v /= d;
    }
    return { v: sign * v, src: this.s.slice(start, this.i).trim() };
  }

  plainNumber(): number | null {
    const m = /^\d+(?:\.\d+)?|^\.\d+/.exec(this.s.slice(this.i));
    if (!m) return null;
    this.i += m[0].length;
    return parseFloat(m[0]);
  }

  // ---- intervals and finite sets
  interval(): Expr | null {
    const save = this.i;
    this.ws();
    const open = this.s[this.i];
    if (open !== "(" && open !== "[" && open !== "]") return null;
    this.i++;
    const a = this.number();
    if (!a || !(this.eat(",") || this.eat(";"))) { this.i = save; return null; }
    const b = this.number();
    this.ws();
    const close = this.s[this.i];
    if (!b || (close !== ")" && close !== "]" && close !== "[")) { this.i = save; return null; }
    this.i++;
    if (a.v > b.v) this.fail("El extremo izquierdo es mayor que el derecho", save, this.i - save);
    return { t: "iv", lo: { v: a.v, closed: open === "[", src: a.src }, hi: { v: b.v, closed: close === "]", src: b.src } };
  }

  finite(): Expr {
    const start = this.i;
    this.i++; // {
    const vals: Array<{ v: number; src: string }> = [];
    this.ws();
    if (this.eat("}")) return { t: "fin", vals };
    for (;;) {
      const n = this.number();
      if (!n) this.fail("Se esperaba un número dentro de { }", this.i);
      vals.push(n);
      if (this.eat(",") || this.eat(";")) continue;
      if (this.eat("}")) return { t: "fin", vals };
      this.fail("Falta cerrar la llave }", start, this.i - start);
    }
  }

  // ---- operands
  atom(): Expr {
    const c = this.peek();
    if (c === undefined) this.fail("La expresión está incompleta", this.s.length, 0);
    if (c === "(" || c === "[" || c === "]") {
      const iv = this.interval();
      if (iv) return iv;
      if (c === "(") {
        const start = this.i;
        this.i++;
        const e = this.union();
        if (!this.eat(")")) this.fail("Falta cerrar el paréntesis )", start, 1);
        return e;
      }
      this.fail(c === "[" ? "Intervalo mal escrito: falta un extremo o el cierre" : "Intervalo mal escrito", this.i);
    }
    if (c === "{") return this.finite();
    if (c === "∅" || c === "Ø") { this.i++; return { t: "fin", vals: [] }; }
    if (c === "¬" || c === "~" || c === "!") { this.i++; return { t: "cp", a: this.postfix() }; }
    if (c === "ℝ") { this.i++; return { t: "ref", name: "R" }; }
    if (c === "Ω") { this.i++; return { t: "ref", name: "U" }; }
    if (isLetter(c)) {
      const m = /^[\p{L}][\p{L}\d_]*/u.exec(this.s.slice(this.i))!;
      // a letter is a set; digits or an underscore after it are a subscript (A1, A_2), and a following letter is the next token
      const name = /^[\p{L}](?:_?\d+)?/u.exec(m[0])![0];
      this.i += name.length;
      return { t: "ref", name: name.toUpperCase() };
    }
    // a bare number is the one-point set {x}
    const n = this.number();
    if (n) return { t: "fin", vals: [n] };
    this.fail(`No entiendo «${c}»`, this.i);
  }

  postfix(): Expr {
    let e = this.atom();
    for (;;) {
      this.ws();
      if (this.eat("'") || this.eat("’") || this.eat("′") || this.eat("ᶜ") || this.eat("^c") || this.eat("^C") || this.eat("^{c}") || this.eat("^{C}")) e = { t: "cp", a: e };
      else return e;
    }
  }

  /** Intersection: ∩, &, *, n, and. */
  private interOp(): boolean {
    this.ws();
    if (this.eat("∩") || this.eat("&") || this.eat("*")) return true;
    if (this.word("and")) return true;
    if ((this.s[this.i] === "n") && !isLetter(this.s[this.i + 1]) && !isDigit(this.s[this.i + 1])) { this.i++; return true; }
    return false;
  }

  inter(): Expr {
    let e = this.postfix();
    while (this.interOp()) e = { t: "in", a: e, b: this.postfix() };
    return e;
  }

  /** Union, difference and symmetric difference, at the same level. */
  union(): Expr {
    let e = this.inter();
    for (;;) {
      this.ws();
      let kind: "un" | "df" | "sd" | null = null;
      if (this.eat("∪") || this.eat("|") || this.eat("+")) kind = "un";
      else if (this.eat("∖") || this.eat("\\") || this.eat("−") || this.eat("–")) kind = "df";
      else if (this.eat("-") ) kind = "df";
      else if (this.eat("Δ") || this.eat("△") || this.eat("▵")) kind = "sd";
      else if (this.word("or")) kind = "un";
      else if (this.word("minus")) kind = "df";
      else if (this.word("xor")) kind = "sd";
      else if ((this.s[this.i] === "U" || this.s[this.i] === "u") && !isLetter(this.s[this.i + 1]) && !isDigit(this.s[this.i + 1]) && this.s[this.i + 1] !== "_") { this.i++; kind = "un"; }
      if (!kind) return e;
      e = { t: kind, a: e, b: this.inter() };
    }
  }
}

/** Parse what was typed. Never throws: a mistake is reported with its place in the text. */
export function parseSetExpr(src: string): Parsed {
  const text = prepare(src);
  const p = new Parser(text);
  try {
    p.ws();
    if (p.i >= text.length) return { ok: false, pos: 0, len: 0, msg: "Escribe una expresión" };
    const expr = p.union();
    p.ws();
    if (p.i < text.length) throw new Fail({ pos: p.i, len: text.length - p.i, msg: `Sobra «${text.slice(p.i, p.i + 8).trim()}»: falta un operador (∪ ∩ ∖ Δ) entre las partes` });
    return { ok: true, expr };
  } catch (e) {
    if (e instanceof Fail) return { ok: false, ...e.err };
    throw e;
  }
}

/** Every name of set used in an expression, in order of appearance. */
export function namesIn(e: Expr, out: string[] = []): string[] {
  switch (e.t) {
    case "ref": if (!out.includes(e.name)) out.push(e.name); break;
    case "cp": namesIn(e.a, out); break;
    case "un": case "in": case "df": case "sd": namesIn(e.a, out); namesIn(e.b, out); break;
    default: break;
  }
  return out;
}

export type Evaluated<T> = { ok: true; value: T } | { ok: false; msg: string };

/** The set of real numbers an expression stands for. `R` and `U` are the whole line; other names have no meaning on the line. */
export function evalReal(e: Expr): Evaluated<IntervalSet> {
  try { return { ok: true, value: real(e) }; } catch (err) { if (err instanceof Fail) return { ok: false, msg: err.err.msg }; throw err; }
}
function real(e: Expr): IntervalSet {
  switch (e.t) {
    case "iv": return normalize([piece(e.lo.v, e.hi.v, e.lo.closed, e.hi.closed)]);
    case "fin": return normalize(e.vals.map((x): Piece => piece(x.v, x.v, true, true)));
    case "ref":
      if (e.name === "R" || e.name === "U") return FULL;
      throw new Fail({ pos: 0, len: 0, msg: `«${e.name}» no es un conjunto de números: en la recta usa intervalos, {…}, R o ∅` });
    case "un": return union(real(e.a), real(e.b));
    case "in": return intersect(real(e.a), real(e.b));
    case "df": return difference(real(e.a), real(e.b));
    case "sd": return symmetric(real(e.a), real(e.b));
    case "cp": return complement(real(e.a));
  }
}

/**
 * For a diagram of k sets: which of the 2^k regions an expression shades. A region is a number whose bit i says "inside set i"
 * (0 is outside all the sets, inside the universe). Intervals have no place in a diagram.
 */
export function evalRegions(e: Expr, names: string[]): Evaluated<boolean[]> {
  try {
    const k = names.length;
    const out: boolean[] = [];
    for (let m = 0; m < 1 << k; m++) out.push(inRegion(e, names, m));
    return { ok: true, value: out };
  } catch (err) { if (err instanceof Fail) return { ok: false, msg: err.err.msg }; throw err; }
}
function inRegion(e: Expr, names: string[], m: number): boolean {
  switch (e.t) {
    case "ref": {
      if (e.name === "U") return true;
      const i = names.findIndex((n) => n.toUpperCase() === e.name);
      if (i < 0) throw new Fail({ pos: 0, len: 0, msg: `No hay un conjunto «${e.name}» en el diagrama (hay ${names.join(", ")})` });
      return (m >> i & 1) === 1;
    }
    case "iv": case "fin": throw new Fail({ pos: 0, len: 0, msg: "Un diagrama de conjuntos no lleva intervalos ni números: usa los nombres de los conjuntos" });
    case "un": return inRegion(e.a, names, m) || inRegion(e.b, names, m);
    case "in": return inRegion(e.a, names, m) && inRegion(e.b, names, m);
    case "df": return inRegion(e.a, names, m) && !inRegion(e.b, names, m);
    case "sd": return inRegion(e.a, names, m) !== inRegion(e.b, names, m);
    case "cp": return !inRegion(e.a, names, m);
  }
}

export { EMPTY };
