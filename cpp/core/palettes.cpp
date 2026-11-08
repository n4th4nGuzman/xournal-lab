#include "palettes.h"

const char* const kDefaultPalette = "classic";

static QStringList L(std::initializer_list<const char*> l) {
  QStringList out;
  for (const char* s : l) out << QString::fromLatin1(s);
  return out;
}

const std::vector<Palette>& palettes() {
  static const std::vector<Palette> all = {
    {"classic", QString::fromUtf8("Clásica"), L({"#000000", "#3333cc", "#ff0000", "#008000", "#ff8000"}), {}, {}},
    {"vivid", "Vivos", L({"#000000", "#d32f2f", "#f57c00", "#fbc02d", "#388e3c", "#1976d2", "#7b1fa2", "#455a64"}), {}, {}},
    {"pastel", "Pastel", L({"#5b5b66", "#f28b82", "#fbbc74", "#fdd663", "#81c995", "#78d9ec", "#8ab4f8", "#c58af9"}), {}, {}},
    {"earth", "Tierra", L({"#2b2118", "#8c4a2f", "#c8803c", "#d9b44a", "#6b8f4e", "#3f6f6a", "#4a6a8a", "#7a4e7a"}), {}, {}},
    {"ocean", QString::fromUtf8("Océano"), L({"#0b132b", "#1c2541", "#3a506b", "#2a9d8f", "#5bc0be", "#457b9d", "#a8dadc", "#e0fbfc"}), {}, {}},
    {"sunset", "Atardecer", L({"#2d142c", "#510a32", "#801336", "#c72c41", "#ee4540", "#f08a4b", "#f6c667", "#ffe3b3"}), {}, {}},
    {"mono", "Grises", L({"#000000", "#333333", "#555555", "#777777", "#999999", "#bbbbbb"}), {}, L({"#ffffff", "#f2f2f2", "#d9d9d9", "#2b2b2b", "#121212"})},
    // Neon tones that keep enough contrast on white paper.
    {"synthwave", "Synthwave (papel claro)", L({"#1a0f38", "#e0147f", "#0aa5c4", "#8a3ffc", "#f26a1b", "#12a97f", "#d4a017", "#d7263d"}), {},
     L({"#ffffff", "#fff7fd", "#f6efff", "#eafaff", "#fff4e8"})},
    // The same family at full brightness, meant for a dark paper (Hoja -> color de papel).
    {"neondreams", "Neon Dreams (papel oscuro)", L({"#ffffff", "#ff2a9d", "#05e1ff", "#b967ff", "#ffe45e", "#2dffb2", "#ff8a3d", "#ff4d6d"}), "#120a26",
     L({"#120a26", "#1a0f38", "#241449", "#0b0618", "#0d1b2a"})},
  };
  return all;
}

const Palette& paletteById(const QString& id) {
  for (const Palette& p : palettes())
    if (p.id == id) return p;
  return palettes().front();
}
