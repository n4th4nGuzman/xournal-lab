/**
 * Symbols and structures offered by the LaTeX dialog, in categories. Every item shows its own rendering as the
 * button label (`tex`), inserts `snippet` at the caret, and can be found by `name`. In a snippet, `¦` marks where the
 * caret goes afterwards (it is never inserted).
 */
export interface PaletteItem {
  /** LaTeX rendered as the button label. */
  tex: string;
  snippet: string;
  /** Spanish name, also what the search looks at (together with the code). */
  name: string;
}

export interface PaletteCategory {
  id: string;
  title: string;
  items: PaletteItem[];
}

export const CARET = "¦";

const R = String.raw;
/** A symbol: its own command is both the label and the snippet. */
const s = (cmd: string, name: string): PaletteItem => ({ tex: cmd, snippet: `${cmd} `, name });
/** A structure: label, snippet with the caret marker, name. */
const t = (tex: string, snippet: string, name: string): PaletteItem => ({ tex, snippet, name });

const STRUCTURES: PaletteItem[] = [
  t(R`\frac{a}{b}`, R`\frac{¦}{}`, "fracción"),
  t(R`\dfrac{a}{b}`, R`\dfrac{¦}{}`, "fracción grande (display)"),
  t(R`\tfrac{a}{b}`, R`\tfrac{¦}{}`, "fracción pequeña (en línea)"),
  t(R`\cfrac{a}{b}`, R`\cfrac{¦}{}`, "fracción continua"),
  t(R`\binom{n}{k}`, R`\binom{¦}{}`, "combinatorio, coeficiente binomial"),
  t(R`x^{n}`, R`^{¦}`, "exponente, potencia, superíndice"),
  t(R`x_{n}`, R`_{¦}`, "subíndice"),
  t(R`x_{i}^{n}`, R`_{¦}^{}`, "subíndice y exponente"),
  t(R`x^{2}`, R`^{2}¦`, "al cuadrado"),
  t(R`x^{3}`, R`^{3}¦`, "al cubo"),
  t(R`x^{-1}`, R`^{-1}¦`, "inverso, elevado a -1"),
  t(R`e^{x}`, R`e^{¦}`, "exponencial e a la x"),
  t(R`10^{n}`, R`10^{¦}`, "potencia de diez"),
  t(R`\sqrt{x}`, R`\sqrt{¦}`, "raíz cuadrada"),
  t(R`\sqrt[n]{x}`, R`\sqrt[¦]{}`, "raíz con índice, raíz n-ésima"),
  t(R`\sqrt[3]{x}`, R`\sqrt[3]{¦}`, "raíz cúbica"),
  t(R`\frac{1}{\sqrt{x}}`, R`\frac{1}{\sqrt{¦}}`, "uno sobre raíz"),
  t(R`\left(x\right)`, R`\left( ¦ \right)`, "paréntesis que se ajustan"),
  t(R`\left[x\right]`, R`\left[ ¦ \right]`, "corchetes que se ajustan"),
  t(R`\left\{x\right\}`, R`\left\{ ¦ \right\}`, "llaves que se ajustan"),
  t(R`\left\langle x\right\rangle`, R`\left\langle ¦ \right\rangle`, "ángulos ⟨ ⟩"),
  t(R`\left|x\right|`, R`\left| ¦ \right|`, "valor absoluto, módulo"),
  t(R`\left\|x\right\|`, R`\left\| ¦ \right\|`, "norma"),
  t(R`\lfloor x\rfloor`, R`\left\lfloor ¦ \right\rfloor`, "parte entera, piso"),
  t(R`\lceil x\rceil`, R`\left\lceil ¦ \right\rceil`, "techo"),
  t(R`\text{texto}`, R`\text{¦}`, "texto normal dentro de la fórmula"),
  t(R`\boxed{x}`, R`\boxed{¦}`, "recuadro, resultado enmarcado"),
  t(R`\overbrace{x}^{n}`, R`\overbrace{¦}^{}`, "llave encima"),
  t(R`\underbrace{x}_{n}`, R`\underbrace{¦}_{}`, "llave debajo"),
  t(R`\overset{a}{=}`, R`\overset{¦}{=}`, "símbolo encima de otro"),
  t(R`\underset{a}{=}`, R`\underset{¦}{=}`, "símbolo debajo de otro"),
  t(R`\xrightarrow{a}`, R`\xrightarrow{¦}`, "flecha con texto encima"),
  t(R`\xleftarrow{a}`, R`\xleftarrow{¦}`, "flecha izquierda con texto"),
  t(R`\cancel{x}`, R`\cancel{¦}`, "tachar, cancelar"),
  t(R`\color{red}{x}`, R`{\color{red} ¦}`, "color rojo"),
  t(R`\substack{a\\b}`, R`\substack{¦ \\ }`, "varias líneas bajo un símbolo"),
];

const CALCULUS: PaletteItem[] = [
  t(R`\sum_{i=1}^{n}`, R`\sum_{i=1}^{n} ¦`, "suma, sumatoria"),
  t(R`\sum_{n=0}^{\infty}`, R`\sum_{n=0}^{\infty} ¦`, "serie infinita"),
  t(R`\prod_{i=1}^{n}`, R`\prod_{i=1}^{n} ¦`, "productoria"),
  t(R`\int`, R`\int ¦ \,dx`, "integral indefinida"),
  t(R`\int_{a}^{b}`, R`\int_{a}^{b} ¦ \,dx`, "integral definida"),
  t(R`\int_{-\infty}^{\infty}`, R`\int_{-\infty}^{\infty} ¦ \,dx`, "integral impropia en toda la recta"),
  t(R`\iint`, R`\iint_{D} ¦ \,dA`, "integral doble"),
  t(R`\iiint`, R`\iiint_{V} ¦ \,dV`, "integral triple"),
  t(R`\oint`, R`\oint_{C} ¦ \,ds`, "integral de línea cerrada, circulación"),
  t(R`\left.f\right|_{a}^{b}`, R`\left. ¦ \right|_{a}^{b}`, "evaluar entre a y b (regla de Barrow)"),
  t(R`\lim_{x\to a}`, R`\lim_{x \to ¦} `, "límite"),
  t(R`\lim_{x\to\infty}`, R`\lim_{x \to \infty} ¦`, "límite en el infinito"),
  t(R`\lim_{x\to 0^{+}}`, R`\lim_{x \to ¦^{+}} `, "límite lateral por la derecha"),
  t(R`\lim_{x\to 0^{-}}`, R`\lim_{x \to ¦^{-}} `, "límite lateral por la izquierda"),
  t(R`\lim_{n\to\infty}`, R`\lim_{n \to \infty} ¦`, "límite de una sucesión"),
  t(R`\limsup`, R`\limsup_{n \to \infty} ¦`, "límite superior"),
  t(R`\liminf`, R`\liminf_{n \to \infty} ¦`, "límite inferior"),
  t(R`\frac{d}{dx}`, R`\frac{d}{dx} ¦`, "derivada respecto de x"),
  t(R`\frac{dy}{dx}`, R`\frac{d¦}{dx}`, "derivada dy/dx"),
  t(R`\frac{d^{2}y}{dx^{2}}`, R`\frac{d^{2}¦}{dx^{2}}`, "segunda derivada"),
  t(R`\frac{d^{n}y}{dx^{n}}`, R`\frac{d^{n}¦}{dx^{n}}`, "derivada n-ésima"),
  t(R`\frac{\partial f}{\partial x}`, R`\frac{\partial ¦}{\partial x}`, "derivada parcial"),
  t(R`\frac{\partial^{2} f}{\partial x^{2}}`, R`\frac{\partial^{2} ¦}{\partial x^{2}}`, "derivada parcial segunda"),
  t(R`\frac{\partial^{2} f}{\partial x\partial y}`, R`\frac{\partial^{2} ¦}{\partial x \partial y}`, "derivada parcial mixta"),
  t(R`f'`, R`f'(¦)`, "f prima"),
  t(R`f''`, R`f''(¦)`, "f segunda"),
  t(R`f^{(n)}`, R`f^{(¦)}`, "derivada n-ésima de f"),
  t(R`\dot{x}`, R`\dot{¦}`, "punto encima (derivada en el tiempo)"),
  t(R`\ddot{x}`, R`\ddot{¦}`, "dos puntos (segunda derivada en el tiempo)"),
  t(R`\nabla f`, R`\nabla ¦`, "gradiente"),
  t(R`\nabla\cdot\vec{F}`, R`\nabla \cdot \vec{¦}`, "divergencia"),
  t(R`\nabla\times\vec{F}`, R`\nabla \times \vec{¦}`, "rotacional"),
  t(R`\nabla^{2}f`, R`\nabla^{2} ¦`, "laplaciano"),
  t(R`\mathrm{d}x`, R`\mathrm{d}¦`, "diferencial d recta"),
  t(R`\Delta x`, R`\Delta ¦`, "incremento delta"),
  t(R`\varepsilon`, R`\varepsilon `, "épsilon (límites)"),
  t(R`\delta`, R`\delta `, "delta (límites)"),
  t(R`O(n)`, R`O(¦)`, "O grande (orden)"),
  t(R`\bigcup_{i}`, R`\bigcup_{i=1}^{n} ¦`, "unión de una familia"),
  t(R`\bigcap_{i}`, R`\bigcap_{i=1}^{n} ¦`, "intersección de una familia"),
];

const matrix = (env: string, rows: number, cols: number, name: string, label: string): PaletteItem => {
  const cell = (r: number, c: number) => (r === 0 && c === 0 ? CARET : "");
  const body = Array.from({ length: rows }, (_, r) => Array.from({ length: cols }, (_, c) => cell(r, c)).join(" & ")).join(R` \\ `);
  return t(label, `\\begin{${env}} ${body} \\end{${env}}`, name);
};

const MATRICES: PaletteItem[] = [
  matrix("pmatrix", 2, 2, "matriz 2×2 con paréntesis", R`\begin{pmatrix}a&b\\c&d\end{pmatrix}`),
  matrix("pmatrix", 3, 3, "matriz 3×3 con paréntesis", R`\begin{pmatrix}a&b&c\\d&e&f\\g&h&i\end{pmatrix}`),
  matrix("pmatrix", 2, 3, "matriz 2×3", R`\begin{pmatrix}a&b&c\\d&e&f\end{pmatrix}`),
  matrix("pmatrix", 3, 2, "matriz 3×2", R`\begin{pmatrix}a&b\\c&d\\e&f\end{pmatrix}`),
  matrix("pmatrix", 2, 1, "vector columna de 2", R`\begin{pmatrix}x\\y\end{pmatrix}`),
  matrix("pmatrix", 3, 1, "vector columna de 3", R`\begin{pmatrix}x\\y\\z\end{pmatrix}`),
  matrix("pmatrix", 1, 3, "vector fila de 3", R`\begin{pmatrix}a&b&c\end{pmatrix}`),
  matrix("bmatrix", 2, 2, "matriz 2×2 con corchetes", R`\begin{bmatrix}a&b\\c&d\end{bmatrix}`),
  matrix("bmatrix", 3, 3, "matriz 3×3 con corchetes", R`\begin{bmatrix}a&b&c\\d&e&f\\g&h&i\end{bmatrix}`),
  matrix("vmatrix", 2, 2, "determinante 2×2", R`\begin{vmatrix}a&b\\c&d\end{vmatrix}`),
  matrix("vmatrix", 3, 3, "determinante 3×3", R`\begin{vmatrix}a&b&c\\d&e&f\\g&h&i\end{vmatrix}`),
  matrix("Vmatrix", 2, 2, "norma de matriz con dobles barras", R`\begin{Vmatrix}a&b\\c&d\end{Vmatrix}`),
  matrix("Bmatrix", 2, 2, "matriz con llaves", R`\begin{Bmatrix}a&b\\c&d\end{Bmatrix}`),
  matrix("matrix", 2, 2, "matriz sin delimitadores", R`\begin{matrix}a&b\\c&d\end{matrix}`),
  t(R`\begin{pmatrix}1&0\\0&1\end{pmatrix}`, R`\begin{pmatrix} 1 & 0 \\ 0 & 1 \end{pmatrix}¦`, "matriz identidad 2×2"),
  t(R`\begin{pmatrix}1&0&0\\0&1&0\\0&0&1\end{pmatrix}`, R`\begin{pmatrix} 1 & 0 & 0 \\ 0 & 1 & 0 \\ 0 & 0 & 1 \end{pmatrix}¦`, "matriz identidad 3×3"),
  t(R`\left(\begin{array}{cc|c}a&b&e\\c&d&f\end{array}\right)`, R`\left( \begin{array}{cc|c} ¦ & & \\ & & \end{array} \right)`, "matriz ampliada de un sistema"),
  t(R`\begin{cases}a\\b\end{cases}`, R`\begin{cases} ¦ \\ \end{cases}`, "sistema o función por partes, 2 casos"),
  t(R`\begin{cases}a\\b\\c\end{cases}`, R`\begin{cases} ¦ \\ \\ \end{cases}`, "sistema o función por partes, 3 casos"),
  t(R`\begin{cases}x&\text{si }x\ge0\\-x&\text{si }x<0\end{cases}`, R`\begin{cases} ¦ & \text{si } \\ & \text{si } \end{cases}`, "función definida por partes con condiciones"),
  t(R`\begin{aligned}a&=b\\&=c\end{aligned}`, R`\begin{aligned} ¦ &= \\ &= \end{aligned}`, "alinear igualdades"),
  t(R`\begin{gathered}a\\b\end{gathered}`, R`\begin{gathered} ¦ \\ \end{gathered}`, "varias líneas centradas"),
  t(R`\begin{array}{c|c}a&b\\\hline c&d\end{array}`, R`\begin{array}{c|c} ¦ & \\ \hline & \end{array}`, "tabla con líneas"),
  s(R`\cdots`, "puntos suspensivos centrados"),
  s(R`\vdots`, "puntos verticales"),
  s(R`\ddots`, "puntos diagonales"),
  s(R`\ldots`, "puntos suspensivos bajos"),
];

const lower = ["alpha", "beta", "gamma", "delta", "epsilon", "varepsilon", "zeta", "eta", "theta", "vartheta", "iota", "kappa", "lambda", "mu", "nu", "xi", "pi", "varpi", "rho", "varrho", "sigma", "varsigma", "tau", "upsilon", "phi", "varphi", "chi", "psi", "omega"];
const upper = ["Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon", "Phi", "Psi", "Omega"];
const greekNames: Record<string, string> = {
  alpha: "alfa", beta: "beta", gamma: "gamma", delta: "delta", epsilon: "épsilon", varepsilon: "épsilon variante", zeta: "dseta", eta: "eta", theta: "theta, zeta",
  vartheta: "theta variante", iota: "iota", kappa: "kappa", lambda: "lambda", mu: "mu", nu: "nu", xi: "xi", pi: "pi", varpi: "pi variante", rho: "ro", varrho: "ro variante",
  sigma: "sigma", varsigma: "sigma final", tau: "tau", upsilon: "ípsilon", phi: "fi", varphi: "fi variante", chi: "ji", psi: "psi", omega: "omega",
};
const GREEK: PaletteItem[] = [
  ...lower.map((n) => s(`\\${n}`, `${greekNames[n]} minúscula`)),
  ...upper.map((n) => s(`\\${n}`, `${(greekNames[n.toLowerCase()] ?? n)} mayúscula`)),
];

const OPERATORS: PaletteItem[] = [
  ...["sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "coth", "log", "ln", "lg", "exp", "min", "max", "sup", "inf", "det", "dim", "ker", "gcd", "arg", "deg", "Pr", "hom"].map((n) => t(`\\${n}`, `\\${n} `, `función ${n}`)),
  t(R`\operatorname{lcm}`, R`\operatorname{mcm}(¦)`, "mínimo común múltiplo"),
  t(R`\operatorname{sgn}`, R`\operatorname{sgn}(¦)`, "signo"),
  t(R`\operatorname{Re}`, R`\operatorname{Re}(¦)`, "parte real"),
  t(R`\operatorname{Im}`, R`\operatorname{Im}(¦)`, "parte imaginaria"),
  t(R`a \bmod n`, R`\bmod ¦`, "módulo (resto)"),
  t(R`\pmod{n}`, R`\pmod{¦}`, "congruente módulo n"),
  s(R`\pm`, "más menos"), s(R`\mp`, "menos más"), s(R`\times`, "por, producto"), s(R`\div`, "dividido"), s(R`\cdot`, "punto, producto escalar"),
  s(R`\ast`, "asterisco"), s(R`\star`, "estrella"), s(R`\circ`, "composición, círculo"), s(R`\bullet`, "viñeta"), s(R`\oplus`, "suma directa, o exclusivo"),
  s(R`\ominus`, "menos circulado"), s(R`\otimes`, "producto tensorial"), s(R`\odot`, "producto de Hadamard"), s(R`\setminus`, "diferencia de conjuntos"),
  s(R`\wedge`, "y lógico, producto cuña"), s(R`\vee`, "o lógico"), s(R`\cap`, "intersección"), s(R`\cup`, "unión"), s(R`\sqcup`, "unión disjunta"),
  s(R`\dagger`, "daga, adjunto"), s(R`\ddagger`, "doble daga"), s(R`\amalg`, "coproducto"),
];

const RELATIONS: PaletteItem[] = [
  s(R`\neq`, "distinto"), s(R`\approx`, "aproximadamente igual"), s(R`\cong`, "congruente"), s(R`\equiv`, "equivalente, idéntico"), s(R`\sim`, "semejante, se distribuye como"),
  s(R`\simeq`, "asintóticamente igual"), s(R`\propto`, "proporcional"), s(R`\doteq`, "igual por definición punto"), s(R`:=`, "se define como"),
  s(R`<`, "menor que"), s(R`>`, "mayor que"), s(R`\leq`, "menor o igual"), s(R`\geq`, "mayor o igual"), s(R`\ll`, "mucho menor"), s(R`\gg`, "mucho mayor"),
  s(R`\prec`, "precede"), s(R`\succ`, "sigue"), s(R`\preceq`, "precede o igual"), s(R`\succeq`, "sigue o igual"), s(R`\nless`, "no menor"), s(R`\ngtr`, "no mayor"),
  s(R`\in`, "pertenece"), s(R`\notin`, "no pertenece"), s(R`\ni`, "contiene como elemento"), s(R`\subset`, "subconjunto"), s(R`\supset`, "superconjunto"),
  s(R`\subseteq`, "subconjunto o igual"), s(R`\supseteq`, "superconjunto o igual"), s(R`\subsetneq`, "subconjunto propio"), s(R`\nsubseteq`, "no es subconjunto"),
  s(R`\mid`, "divide a"), s(R`\nmid`, "no divide a"), s(R`\parallel`, "paralelo"), s(R`\nparallel`, "no paralelo"), s(R`\perp`, "perpendicular"),
  s(R`\vdash`, "demuestra, deduce"), s(R`\models`, "satisface, modela"), s(R`\asymp`, "del mismo orden"), s(R`\bowtie`, "reunión (join)"),
];

const ARROWS: PaletteItem[] = [
  s(R`\to`, "flecha a la derecha"), s(R`\leftarrow`, "flecha a la izquierda"), s(R`\leftrightarrow`, "flecha doble sentido"), s(R`\mapsto`, "se transforma en"),
  s(R`\longrightarrow`, "flecha larga"), s(R`\longleftarrow`, "flecha larga izquierda"), s(R`\longleftrightarrow`, "flecha larga doble"), s(R`\longmapsto`, "mapea largo"),
  s(R`\Rightarrow`, "implica"), s(R`\Leftarrow`, "implicado por"), s(R`\Leftrightarrow`, "si y solo si"), s(R`\iff`, "si y solo si (largo)"),
  s(R`\implies`, "implica (largo)"), s(R`\impliedby`, "es implicado por (largo)"),
  s(R`\uparrow`, "flecha arriba"), s(R`\downarrow`, "flecha abajo"), s(R`\updownarrow`, "flecha arriba y abajo"),
  s(R`\Uparrow`, "doble flecha arriba"), s(R`\Downarrow`, "doble flecha abajo"), s(R`\nearrow`, "flecha noreste"), s(R`\searrow`, "flecha sureste"),
  s(R`\nwarrow`, "flecha noroeste"), s(R`\swarrow`, "flecha suroeste"), s(R`\rightleftharpoons`, "equilibrio, reacción reversible"),
  s(R`\leftrightharpoons`, "equilibrio invertido"), s(R`\rightharpoonup`, "arpón derecha (vector)"), s(R`\hookrightarrow`, "inclusión, flecha con gancho"),
  s(R`\twoheadrightarrow`, "sobreyectiva"), s(R`\rightrightarrows`, "dos flechas"), s(R`\circlearrowleft`, "giro antihorario"), s(R`\circlearrowright`, "giro horario"),
];

const SETS: PaletteItem[] = [
  t(R`\mathbb{R}`, R`\mathbb{R}`, "números reales"), t(R`\mathbb{N}`, R`\mathbb{N}`, "números naturales"), t(R`\mathbb{Z}`, R`\mathbb{Z}`, "números enteros"),
  t(R`\mathbb{Q}`, R`\mathbb{Q}`, "números racionales"), t(R`\mathbb{C}`, R`\mathbb{C}`, "números complejos"), t(R`\mathbb{P}`, R`\mathbb{P}`, "probabilidad, primos"),
  t(R`\mathbb{E}`, R`\mathbb{E}`, "esperanza"), t(R`\mathbb{R}^{n}`, R`\mathbb{R}^{¦}`, "espacio euclídeo R^n"), t(R`\mathbb{R}^{2}`, R`\mathbb{R}^{2}`, "plano R²"),
  t(R`\mathbb{R}^{3}`, R`\mathbb{R}^{3}`, "espacio R³"), t(R`\mathbb{1}`, R`\mathbb{1}`, "indicadora"),
  s(R`\emptyset`, "conjunto vacío"), s(R`\varnothing`, "conjunto vacío (variante)"), s(R`\infty`, "infinito"), s(R`\aleph`, "álef"),
  t(R`\mathcal{P}(A)`, R`\mathcal{P}(¦)`, "partes de un conjunto"), t(R`\{x \mid x\in A\}`, R`\{ ¦ \mid \}`, "conjunto por comprensión"),
  t(R`\{1,2,\ldots,n\}`, R`\{1, 2, \ldots, ¦\}`, "conjunto de 1 a n"), t(R`[a,b]`, R`[¦, ]`, "intervalo cerrado"), t(R`(a,b)`, R`(¦, )`, "intervalo abierto"),
  t(R`[a,b)`, R`[¦, )`, "intervalo semiabierto"), t(R`A^{c}`, R`^{c}¦`, "complemento"), t(R`\overline{A}`, R`\overline{¦}`, "complemento o clausura con barra"),
  s(R`\forall`, "para todo"), s(R`\exists`, "existe"), s(R`\nexists`, "no existe"), t(R`\exists!`, R`\exists! `, "existe un único"), s(R`\neg`, "negación"),
  s(R`\land`, "y lógico"), s(R`\lor`, "o lógico"), s(R`\top`, "verdadero"), s(R`\bot`, "falso, perpendicular"), s(R`\therefore`, "por lo tanto"),
  s(R`\because`, "porque"), s(R`\square`, "cuadrado, fin de demostración"), s(R`\blacksquare`, "fin de demostración relleno"), s(R`\triangle`, "triángulo"),
  s(R`\angle`, "ángulo"), s(R`\measuredangle`, "ángulo medido"), s(R`\degree`, "grado"), t(R`^{\circ}`, R`^{\circ}`, "grados"),
];

const ACCENTS: PaletteItem[] = [
  t(R`\vec{x}`, R`\vec{¦}`, "vector con flecha"), t(R`\overrightarrow{AB}`, R`\overrightarrow{¦}`, "vector largo AB"), t(R`\hat{x}`, R`\hat{¦}`, "gorro, versor"),
  t(R`\widehat{ABC}`, R`\widehat{¦}`, "gorro ancho (ángulo)"), t(R`\bar{x}`, R`\bar{¦}`, "barra, media"), t(R`\overline{AB}`, R`\overline{¦}`, "barra larga (segmento)"),
  t(R`\underline{x}`, R`\underline{¦}`, "subrayado"), t(R`\tilde{x}`, R`\tilde{¦}`, "tilde"), t(R`\widetilde{xy}`, R`\widetilde{¦}`, "tilde ancha"),
  t(R`\dot{x}`, R`\dot{¦}`, "un punto"), t(R`\ddot{x}`, R`\ddot{¦}`, "dos puntos"), t(R`\dddot{x}`, R`\dddot{¦}`, "tres puntos"), t(R`\acute{x}`, R`\acute{¦}`, "acento agudo"),
  t(R`\grave{x}`, R`\grave{¦}`, "acento grave"), t(R`\breve{x}`, R`\breve{¦}`, "breve"), t(R`\check{x}`, R`\check{¦}`, "caron"), t(R`\mathring{x}`, R`\mathring{¦}`, "anillo"),
  t(R`\mathbf{x}`, R`\mathbf{¦}`, "negrita"), t(R`\boldsymbol{\alpha}`, R`\boldsymbol{¦}`, "negrita para símbolos griegos"), t(R`\mathit{x}`, R`\mathit{¦}`, "cursiva"),
  t(R`\mathrm{x}`, R`\mathrm{¦}`, "recta (romana)"), t(R`\mathsf{x}`, R`\mathsf{¦}`, "sin serifa"), t(R`\mathtt{x}`, R`\mathtt{¦}`, "máquina de escribir"),
  t(R`\mathcal{L}`, R`\mathcal{¦}`, "caligráfica"), t(R`\mathfrak{g}`, R`\mathfrak{¦}`, "gótica"), t(R`\mathbb{R}`, R`\mathbb{¦}`, "doble trazo (pizarra)"),
  t(R`\displaystyle\sum`, R`{\displaystyle ¦}`, "tamaño de display"), t(R`\scriptstyle x`, R`{\scriptstyle ¦}`, "tamaño de subíndice"),
  t(R`\ell`, R`\ell `, "ele cursiva"), t(R`\hbar`, R`\hbar `, "h barra (Planck reducida)"), t(R`\imath`, R`\imath `, "i sin punto"),
  t(R`\prime`, R`^{\prime}`, "prima"),
];

const SPACES: PaletteItem[] = [
  t(R`a\,b`, R`\,`, "espacio fino"), t(R`a\;b`, R`\;`, "espacio medio"), t(R`a\quad b`, R`\quad `, "espacio grande"), t(R`a\qquad b`, R`\qquad `, "espacio muy grande"),
  t(R`a\!b`, R`\!`, "espacio negativo (juntar)"), t(R`\ `, R`\ `, "espacio normal"), t(R`\\`, R`\\ `, "salto de línea"),
  t(R`\phantom{x}`, R`\phantom{¦}`, "espacio invisible del ancho de algo"),
];

const PHYSICS: PaletteItem[] = [
  t(R`\vec{F}=m\vec{a}`, R`\vec{F} = m\vec{a}`, "segunda ley de Newton"),
  t(R`\vec{F}`, R`\vec{F}`, "fuerza vector"), t(R`\vec{v}`, R`\vec{v}`, "velocidad vector"), t(R`\vec{a}`, R`\vec{a}`, "aceleración vector"),
  t(R`\vec{E}`, R`\vec{E}`, "campo eléctrico"), t(R`\vec{B}`, R`\vec{B}`, "campo magnético"), t(R`\vec{r}`, R`\vec{r}`, "posición vector"),
  t(R`\hat{\imath}\ \hat{\jmath}\ \hat{k}`, R`\hat{\imath} \ \hat{\jmath} \ \hat{k} `, "versores i j k"),
  t(R`\vec{a}\cdot\vec{b}`, R`\vec{¦} \cdot \vec{}`, "producto escalar"), t(R`\vec{a}\times\vec{b}`, R`\vec{¦} \times \vec{}`, "producto vectorial"),
  t(R`\left|\vec{v}\right|`, R`\left| \vec{¦} \right|`, "módulo de un vector"),
  t(R`\oint\vec{E}\cdot d\vec{A}`, R`\oint \vec{E} \cdot d\vec{A} = \frac{Q}{\varepsilon_{0}}`, "ley de Gauss"),
  t(R`\oint\vec{B}\cdot d\vec{\ell}`, R`\oint \vec{B} \cdot d\vec{\ell} = \mu_{0} I`, "ley de Ampère"),
  t(R`\nabla\times\vec{E}=-\frac{\partial\vec{B}}{\partial t}`, R`\nabla \times \vec{E} = -\frac{\partial \vec{B}}{\partial t}`, "ley de Faraday diferencial"),
  t(R`\frac{1}{2}mv^{2}`, R`\frac{1}{2} m v^{2}`, "energía cinética"), t(R`mgh`, R`m g h`, "energía potencial"), t(R`E=mc^{2}`, R`E = mc^{2}`, "energía en reposo"),
  t(R`\hbar`, R`\hbar `, "h barra"), t(R`\mu_{0}`, R`\mu_{0}`, "permeabilidad del vacío"), t(R`\varepsilon_{0}`, R`\varepsilon_{0}`, "permitividad del vacío"),
  t(R`\langle\psi|`, R`\langle ¦ |`, "bra de Dirac"), t(R`|\psi\rangle`, R`| ¦ \rangle`, "ket de Dirac"), t(R`\langle\phi|\psi\rangle`, R`\langle ¦ | \rangle`, "producto interno bra-ket"),
  t(R`\hat{H}\psi=E\psi`, R`\hat{H} \psi = E \psi`, "ecuación de Schrödinger estacionaria"),
  t(R`i\hbar\frac{\partial\psi}{\partial t}`, R`i\hbar \frac{\partial \psi}{\partial t} = \hat{H} \psi`, "ecuación de Schrödinger"),
  t(R`\Delta x\,\Delta p\ge\frac{\hbar}{2}`, R`\Delta x \, \Delta p \ge \frac{\hbar}{2}`, "principio de incertidumbre"),
  t(R`\mathrm{m/s^{2}}`, R`\,\mathrm{m/s^{2}}`, "unidad metros por segundo al cuadrado"), t(R`\mathrm{kg}`, R`\,\mathrm{kg}`, "unidad kilogramo"), t(R`\mathrm{N}`, R`\,\mathrm{N}`, "unidad newton"),
  t(R`\mathrm{J}`, R`\,\mathrm{J}`, "unidad joule"), t(R`\mathrm{W}`, R`\,\mathrm{W}`, "unidad watt"), t(R`\mathrm{Hz}`, R`\,\mathrm{Hz}`, "unidad hertz"),
  t(R`\Omega`, R`\,\Omega`, "ohm"), t(R`\mathrm{V}`, R`\,\mathrm{V}`, "unidad volt"), t(R`\mathrm{A}`, R`\,\mathrm{A}`, "unidad ampere"),
  t(R`\times10^{n}`, R`\times 10^{¦}`, "notación científica"), t(R`\pm\Delta x`, R`\pm \Delta ¦`, "incertidumbre"),
];

const VCALC: PaletteItem[] = [
  // operadores diferenciales
  t(R`\nabla f`, R`\nabla ¦`, "vector gradiente, nabla"),
  t(R`\nabla f=\left(\tfrac{\partial f}{\partial x},\tfrac{\partial f}{\partial y},\tfrac{\partial f}{\partial z}\right)`, R`\nabla f = \left( \frac{\partial f}{\partial x}, \frac{\partial f}{\partial y}, \frac{\partial f}{\partial z} \right)`, "gradiente en componentes"),
  t(R`\operatorname{grad} f`, R`\operatorname{grad} ¦`, "gradiente (grad)"),
  t(R`\nabla\cdot\vec{F}`, R`\nabla \cdot \vec{¦}`, "divergencia"),
  t(R`\nabla\cdot\vec{F}=\tfrac{\partial P}{\partial x}+\tfrac{\partial Q}{\partial y}+\tfrac{\partial R}{\partial z}`, R`\nabla \cdot \vec{F} = \frac{\partial P}{\partial x} + \frac{\partial Q}{\partial y} + \frac{\partial R}{\partial z}`, "divergencia en componentes"),
  t(R`\operatorname{div}\vec{F}`, R`\operatorname{div} \vec{¦}`, "divergencia (div)"),
  t(R`\nabla\times\vec{F}`, R`\nabla \times \vec{¦}`, "rotor, rotacional"),
  t(R`\operatorname{rot}\vec{F}`, R`\operatorname{rot} \vec{¦}`, "rotor (rot)"),
  t(R`\operatorname{curl}\vec{F}`, R`\operatorname{curl} \vec{¦}`, "rotacional (curl)"),
  t(R`\nabla\times\vec{F}=\begin{vmatrix}\hat\imath&\hat\jmath&\hat k\\\partial_x&\partial_y&\partial_z\\P&Q&R\end{vmatrix}`, R`\nabla \times \vec{F} = \begin{vmatrix} \hat\imath & \hat\jmath & \hat k \\ \partial_x & \partial_y & \partial_z \\ P & Q & R \end{vmatrix}`, "rotor como determinante"),
  t(R`\nabla^{2}f`, R`\nabla^{2} ¦`, "laplaciano"),
  t(R`\nabla^{2}f=f_{xx}+f_{yy}+f_{zz}`, R`\nabla^{2} f = \frac{\partial^{2} f}{\partial x^{2}} + \frac{\partial^{2} f}{\partial y^{2}} + \frac{\partial^{2} f}{\partial z^{2}}`, "laplaciano en componentes"),
  t(R`\Delta f`, R`\Delta ¦`, "laplaciano (delta)"),
  t(R`D_{\vec u}f=\nabla f\cdot\vec u`, R`D_{\vec{u}} f = \nabla f \cdot \vec{u}`, "derivada direccional"),
  t(R`\frac{\partial f}{\partial \vec n}`, R`\frac{\partial ¦}{\partial \vec{n}}`, "derivada normal"),
  t(R`\frac{\partial(x,y)}{\partial(u,v)}`, R`\frac{\partial(x, y)}{\partial(u, v)}`, "jacobiano"),
  t(R`\begin{pmatrix}f_x&f_y\\g_x&g_y\end{pmatrix}`, R`\begin{pmatrix} f_x & f_y \\ g_x & g_y \end{pmatrix}`, "matriz jacobiana"),
  t(R`\begin{pmatrix}f_{xx}&f_{xy}\\f_{yx}&f_{yy}\end{pmatrix}`, R`\begin{pmatrix} f_{xx} & f_{xy} \\ f_{yx} & f_{yy} \end{pmatrix}`, "matriz hessiana"),
  t(R`D=f_{xx}f_{yy}-f_{xy}^{2}`, R`D = f_{xx} f_{yy} - f_{xy}^{2}`, "discriminante del criterio de la segunda derivada"),
  t(R`\nabla f=\lambda\nabla g`, R`\nabla f = \lambda \nabla g`, "multiplicadores de Lagrange"),
  t(R`f_x`, R`f_{x}`, "derivada parcial como subíndice"), t(R`f_{xy}`, R`f_{xy}`, "derivada parcial mixta subíndice"),
  t(R`df=f_x\,dx+f_y\,dy`, R`df = \frac{\partial f}{\partial x} dx + \frac{\partial f}{\partial y} dy`, "diferencial total"),
  t(R`\frac{dz}{dt}=\tfrac{\partial z}{\partial x}\tfrac{dx}{dt}+\tfrac{\partial z}{\partial y}\tfrac{dy}{dt}`, R`\frac{dz}{dt} = \frac{\partial z}{\partial x}\frac{dx}{dt} + \frac{\partial z}{\partial y}\frac{dy}{dt}`, "regla de la cadena multivariable"),
  t(R`z-z_0=f_x(x-x_0)+f_y(y-y_0)`, R`z - z_{0} = f_{x}(x_{0}, y_{0})(x - x_{0}) + f_{y}(x_{0}, y_{0})(y - y_{0})`, "plano tangente"),
  t(R`\lim_{(x,y)\to(a,b)}`, R`\lim_{(x, y) \to (¦, )} `, "límite en dos variables"),
  t(R`\lim_{(x,y)\to(0,0)}`, R`\lim_{(x, y) \to (0, 0)} ¦`, "límite en el origen"),
  // curvas y vectores
  t(R`\vec{r}(t)=\langle x(t),y(t),z(t)\rangle`, R`\vec{r}(t) = \langle x(t), y(t), z(t) \rangle`, "curva parametrizada, función vectorial"),
  t(R`\vec{r}\,'(t)`, R`\vec{r}\,'(t)`, "vector velocidad, derivada de r"),
  t(R`\|\vec{v}\|`, R`\| \vec{¦} \|`, "norma de un vector"),
  t(R`\hat{T}\ \hat{N}\ \hat{B}`, R`\hat{T} \ \hat{N} \ \hat{B} `, "triedro de Frenet, tangente normal binormal"),
  t(R`\kappa=\frac{\|\vec{T}\,'\|}{\|\vec{r}\,'\|}`, R`\kappa = \frac{\| \vec{T}\,' \|}{\| \vec{r}\,' \|}`, "curvatura"),
  t(R`\vec{a}\times\vec{b}`, R`\vec{¦} \times \vec{}`, "producto vectorial, producto cruz"),
  t(R`\begin{vmatrix}\hat\imath&\hat\jmath&\hat k\\a_1&a_2&a_3\\b_1&b_2&b_3\end{vmatrix}`, R`\vec{a} \times \vec{b} = \begin{vmatrix} \hat\imath & \hat\jmath & \hat k \\ a_{1} & a_{2} & a_{3} \\ b_{1} & b_{2} & b_{3} \end{vmatrix}`, "producto vectorial como determinante"),
  t(R`\vec{a}\cdot\vec{b}`, R`\vec{¦} \cdot \vec{}`, "producto escalar, producto punto"),
  t(R`\langle a,b\rangle`, R`\langle ¦, \rangle`, "vector entre ángulos, producto interno"),
  t(R`\vec{a}\cdot(\vec{b}\times\vec{c})`, R`\vec{a} \cdot (\vec{b} \times \vec{c})`, "producto mixto, triple producto escalar"),
  t(R`\vec{a}\times(\vec{b}\times\vec{c})`, R`\vec{a} \times (\vec{b} \times \vec{c})`, "triple producto vectorial"),
  t(R`\|\vec{a}\times\vec{b}\|=\|\vec{a}\|\|\vec{b}\|\sin\theta`, R`\| \vec{a} \times \vec{b} \| = \| \vec{a} \| \, \| \vec{b} \| \sin\theta`, "módulo del producto vectorial"),
  t(R`\cos\theta=\frac{\vec{a}\cdot\vec{b}}{\|\vec{a}\|\|\vec{b}\|}`, R`\cos\theta = \frac{\vec{a} \cdot \vec{b}}{\| \vec{a} \| \, \| \vec{b} \|}`, "ángulo entre vectores"),
  t(R`\operatorname{proj}_{\vec b}\vec a`, R`\operatorname{proj}_{\vec{b}} \vec{a} = \frac{\vec{a} \cdot \vec{b}}{\| \vec{b} \|^{2}} \vec{b}`, "proyección de un vector sobre otro"),
  t(R`\vec{0}`, R`\vec{0}`, "vector cero, nulo"),
  t(R`\hat{n}`, R`\hat{n}`, "normal unitaria"),
  t(R`\hat{e}_r\ \hat{e}_\theta\ \hat{e}_\phi`, R`\hat{e}_{r} \ \hat{e}_{\theta} \ \hat{e}_{\phi} `, "versores esféricos, polares"),
  // integrales de línea
  t(R`\int_C f\,ds`, R`\int_{C} ¦ \, ds`, "integral de línea de un campo escalar"),
  t(R`\int_C\vec{F}\cdot d\vec{r}`, R`\int_{C} \vec{F} \cdot d\vec{r}`, "integral de línea de un campo vectorial, trabajo"),
  t(R`\int_C P\,dx+Q\,dy`, R`\int_{C} P \, dx + Q \, dy`, "integral de línea en forma diferencial"),
  t(R`\oint_C\vec{F}\cdot d\vec{r}`, R`\oint_{C} \vec{F} \cdot d\vec{r}`, "circulación, integral de línea cerrada"),
  t(R`\int_a^b\vec{F}(\vec{r}(t))\cdot\vec{r}\,'(t)\,dt`, R`\int_{a}^{b} \vec{F}(\vec{r}(t)) \cdot \vec{r}\,'(t) \, dt`, "integral de línea parametrizada"),
  t(R`\int_a^b\|\vec{r}\,'(t)\|\,dt`, R`\int_{a}^{b} \| \vec{r}\,'(t) \| \, dt`, "longitud de arco"),
  t(R`d\vec{r}`, R`d\vec{r}`, "diferencial de posición, elemento de línea"),
  // integrales múltiples y de superficie
  t(R`\iint_D f\,dA`, R`\iint_{D} ¦ \, dA`, "integral doble sobre una región"),
  t(R`\int_a^b\!\!\int_c^d f\,dy\,dx`, R`\int_{a}^{b} \int_{c}^{d} ¦ \, dy \, dx`, "integral iterada, rectángulo"),
  t(R`\int_a^b\!\!\int_{g_1(x)}^{g_2(x)}`, R`\int_{a}^{b} \int_{g_{1}(x)}^{g_{2}(x)} ¦ \, dy \, dx`, "integral iterada, región tipo I"),
  t(R`\iint_D f\,r\,dr\,d\theta`, R`\iint_{D} f(r\cos\theta, r\sin\theta) \, r \, dr \, d\theta`, "integral doble en polares"),
  t(R`\iiint_E f\,dV`, R`\iiint_{E} ¦ \, dV`, "integral triple"),
  t(R`\iiint f\,r\,dz\,dr\,d\theta`, R`\iiint_{E} f \, r \, dz \, dr \, d\theta`, "integral triple en cilíndricas"),
  t(R`\iiint f\,\rho^{2}\sin\phi\,d\rho\,d\phi\,d\theta`, R`\iiint_{E} f \, \rho^{2} \sin\phi \, d\rho \, d\phi \, d\theta`, "integral triple en esféricas"),
  t(R`\iint_S f\,dS`, R`\iint_{S} ¦ \, dS`, "integral de superficie de un campo escalar"),
  t(R`\iint_S\vec{F}\cdot d\vec{S}`, R`\iint_{S} \vec{F} \cdot d\vec{S}`, "flujo a través de una superficie"),
  t(R`\iint_S\vec{F}\cdot\hat{n}\,dS`, R`\iint_{S} \vec{F} \cdot \hat{n} \, dS`, "flujo con la normal unitaria"),
  t(R`\oiint_S\vec{F}\cdot d\vec{S}`, R`\oiint_{S} \vec{F} \cdot d\vec{S}`, "flujo a través de una superficie cerrada"),
  t(R`\iint_D\|\vec{r}_u\times\vec{r}_v\|\,dA`, R`\iint_{D} \| \vec{r}_{u} \times \vec{r}_{v} \| \, dA`, "área de una superficie parametrizada"),
  t(R`dS`, R`dS`, "elemento de superficie"), t(R`dA`, R`dA`, "elemento de área"), t(R`dV`, R`dV`, "elemento de volumen"),
  // teoremas
  t(R`\oint_C P\,dx+Q\,dy=\iint_D\left(\tfrac{\partial Q}{\partial x}-\tfrac{\partial P}{\partial y}\right)dA`, R`\oint_{C} P \, dx + Q \, dy = \iint_{D} \left( \frac{\partial Q}{\partial x} - \frac{\partial P}{\partial y} \right) dA`, "teorema de Green"),
  t(R`\oint_C\vec{F}\cdot\hat{n}\,ds=\iint_D\nabla\cdot\vec{F}\,dA`, R`\oint_{C} \vec{F} \cdot \hat{n} \, ds = \iint_{D} \nabla \cdot \vec{F} \, dA`, "teorema de Green, forma de flujo (divergencia en el plano)"),
  t(R`\oint_C\vec{F}\cdot d\vec{r}=\iint_S(\nabla\times\vec{F})\cdot d\vec{S}`, R`\oint_{C} \vec{F} \cdot d\vec{r} = \iint_{S} (\nabla \times \vec{F}) \cdot d\vec{S}`, "teorema de Stokes"),
  t(R`\oiint_S\vec{F}\cdot d\vec{S}=\iiint_E\nabla\cdot\vec{F}\,dV`, R`\oiint_{S} \vec{F} \cdot d\vec{S} = \iiint_{E} \nabla \cdot \vec{F} \, dV`, "teorema de la divergencia, de Gauss"),
  t(R`\int_C\nabla f\cdot d\vec{r}=f(B)-f(A)`, R`\int_{C} \nabla f \cdot d\vec{r} = f(\vec{r}(b)) - f(\vec{r}(a))`, "teorema fundamental de las integrales de línea"),
  t(R`\vec{F}=\nabla f`, R`\vec{F} = \nabla ¦`, "campo conservativo, función potencial"),
  t(R`\nabla\times\vec{F}=\vec{0}`, R`\nabla \times \vec{F} = \vec{0}`, "campo irrotacional"),
  t(R`\nabla\cdot\vec{F}=0`, R`\nabla \cdot \vec{F} = 0`, "campo solenoidal, sin divergencia"),
  t(R`\nabla^{2}\phi=0`, R`\nabla^{2} \phi = 0`, "ecuación de Laplace"),
  t(R`\nabla\times(\nabla f)=\vec{0}`, R`\nabla \times (\nabla f) = \vec{0}`, "rotor del gradiente"),
  t(R`\nabla\cdot(\nabla\times\vec{F})=0`, R`\nabla \cdot (\nabla \times \vec{F}) = 0`, "divergencia del rotor"),
  t(R`\nabla\times(\nabla\times\vec{F})`, R`\nabla \times (\nabla \times \vec{F}) = \nabla (\nabla \cdot \vec{F}) - \nabla^{2} \vec{F}`, "rotor del rotor"),
];

export const PALETTE_CATEGORIES: PaletteCategory[] = [
  { id: "struct", title: "Estructuras", items: STRUCTURES },
  { id: "calc", title: "Cálculo", items: CALCULUS },
  { id: "vcalc", title: "Cálculo vectorial", items: VCALC },
  { id: "matrix", title: "Matrices y sistemas", items: MATRICES },
  { id: "greek", title: "Griegas", items: GREEK },
  { id: "ops", title: "Operadores y funciones", items: OPERATORS },
  { id: "rel", title: "Relaciones", items: RELATIONS },
  { id: "arrows", title: "Flechas", items: ARROWS },
  { id: "sets", title: "Conjuntos y lógica", items: SETS },
  { id: "accents", title: "Acentos y fuentes", items: ACCENTS },
  { id: "phys", title: "Física", items: PHYSICS },
  { id: "space", title: "Espacios", items: SPACES },
];

const norm = (v: string) => v.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();

/** Items whose name or code contains every word of `query` (accents and case ignored). */
export function searchPalette(query: string): PaletteItem[] {
  const words = norm(query).split(/\s+/).filter(Boolean);
  if (!words.length) return [];
  const seen = new Set<string>();
  const out: PaletteItem[] = [];
  for (const cat of PALETTE_CATEGORIES) {
    for (const it of cat.items) {
      const hay = norm(`${it.name} ${it.snippet} ${it.tex} ${cat.title}`);
      if (words.every((w) => hay.includes(w)) && !seen.has(it.snippet)) { seen.add(it.snippet); out.push(it); }
    }
  }
  return out;
}
