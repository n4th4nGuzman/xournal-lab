// node gen_hostile.mjs OUTDIR : writes hostile .xopp files for the whole-application check (run_app_fuzz.ps1).
// name starting with "bad_" must be refused with a message; "ok_" must open, render and exit cleanly.
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
const out = process.argv[2];
fs.mkdirSync(out, { recursive: true });
const head = '<?xml version="1.0" standalone="no"?>\n<xournal creator="fuzz" fileversion="4"><title>t</title>\n';
const bgDefault = '<background type="solid" color="#ffffffff" style="lined"/>';
const page = (layer, attrs = 'width="595.27" height="841.89"', bg = bgDefault) => `<page ${attrs}>${bg}<layer>${layer}</layer></page>\n`;
const write = (name, xml) => fs.writeFileSync(path.join(out, name + ".xopp"), zlib.gzipSync(Buffer.from(head + xml + "</xournal>")));
const st = (w, pts, extra = "") => `<stroke tool="pen" color="#000000ff" width="${w}" ${extra}>${pts}</stroke>\n`;

fs.writeFileSync(path.join(out, "bad_empty.xopp"), "");
fs.writeFileSync(path.join(out, "bad_garbage.xopp"), "this is not a document\n".repeat(50));
const good = zlib.gzipSync(Buffer.from(head + page(st(1.4, "10 10 100 100 200 50")) + "</xournal>"));
fs.writeFileSync(path.join(out, "bad_truncated_gz.xopp"), good.subarray(0, good.length - 12));
fs.writeFileSync(path.join(out, "bad_gz_header_only.xopp"), good.subarray(0, 12));
write("bad_nan", page(st(1, "nan 1 2 3")));
write("bad_odd_coords", page(st(1, "1 2 3")));
write("bad_wrong_root", "");
fs.writeFileSync(path.join(out, "bad_wrong_root.xopp"), zlib.gzipSync(Buffer.from("<html><body/></html>")));
fs.writeFileSync(path.join(out, "bad_unclosed.xopp"), zlib.gzipSync(Buffer.from(head + "<page width='1' height='1'><layer>")));
write("ok_unknown_elements", `<audio fn="a.mp3"/><page width="300" height="300">${bgDefault}<timeline x="1"/><layer name="L"><timestamp ts="1" fn="a.mp3"/>${st(2, "10 10 100 100")}<custom><x>1</x></custom></layer></page>\n<tail/>`);
write("ok_zero_one_point", page(st(2, "") + st(2, "50 50") + st(2, "10 10 10 10") + '<stroke tool="pen" color="#ff0000ff" width=""/>'));
write("ok_huge_coords", page(st(2, "-1e300 -1e300 1e300 1e300 5 5") + st(1e9, "100 100") + st(2, "1.7e308 1.7e308 -1.7e308 5") + '<text font="Sans" size="1e9" x="1e300" y="5" color="#000000ff">x</text>'));
write("ok_absurd_pages", page(st(2, "1 1 100 100"), 'width="1e9" height="1e9"') + page("", 'width="0" height="-5"') + page("", 'width="nan" height="inf"') + page("", "") + page("", 'width="1e-9" height="1e-9"') + page("", 'width="20000" height="20000"'));
write("ok_garbage_attrs", page('<stroke tool="zzz" color="red" width="-3 0" glow="1e9" glowAlpha="nan" capStyle="?" style="???">1 1 90 90 50 20</stroke>\n<image left="a" top="b" right="c" bottom="d">!!!</image>\n<teximage text="a_over_b" left="1" top="1" right="1" bottom="1">xx</teximage>\n', 'width="300" height="300"', '<background type="pdf" domain="absolute" filename="Z:/no/such.pdf" pageno="99999999999" xlPaper="cart;quad=zzz;unit=-1e9"/>'));
// size
{
  let s = "";
  for (let i = 0; i < 100000; i++) { const x = (i * 37) % 590, y = (i * 91) % 830; let p = ""; for (let k = 0; k < 8; k++) p += `${x + k * 2.5} ${y + (k % 3)} `; s += st(1.41, p.trim()); }
  write("ok_100k_strokes", page(s));
}
{
  let s = "";
  for (let i = 0; i < 500; i++) s += page(st(2, `10 10 ${100 + i} 200`) + `<text font="Sans" size="12" x="50" y="50" color="#000000ff">p${i}</text>`);
  write("ok_500_pages", s);
}
{ // a 50 MB payload that is a valid PNG in its first bytes only (decoding must fail or be refused, not allocate)
  const b = Buffer.alloc(37 * 1024 * 1024); for (let i = 0; i < b.length; i += 4) b.writeUInt32LE((Math.random() * 4294967296) >>> 0, i);
  write("ok_50mb_image", page(`<image left="10" top="10" right="300" bottom="300">${b.toString("base64")}</image>`));
}
console.log("written to " + out);
