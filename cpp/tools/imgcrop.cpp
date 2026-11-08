// xl_imgcrop in.png x y w h out.png [zoom]: crops a PNG (pixels of the input) and optionally enlarges the crop (nearest neighbour), to look at details.
#include <QGuiApplication>
#include <QImage>
#include <cstdio>

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  if (argc < 7) { std::fprintf(stderr, "usage: xl_imgcrop in.png x y w h out.png [zoom]\n"); return 2; }
  QImage in(QString::fromLocal8Bit(argv[1]));
  if (in.isNull()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
  QImage c = in.copy(QString(argv[2]).toInt(), QString(argv[3]).toInt(), QString(argv[4]).toInt(), QString(argv[5]).toInt());
  const int z = argc > 7 ? QString(argv[7]).toInt() : 1;
  if (z > 1) c = c.scaled(c.width() * z, c.height() * z, Qt::IgnoreAspectRatio, Qt::FastTransformation);
  return c.save(QString::fromLocal8Bit(argv[6])) ? 0 : 1;
}
