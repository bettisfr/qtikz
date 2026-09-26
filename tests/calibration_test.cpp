#include "compileservice.h"
#include "pdfcanvas.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <cmath>

class calibrationtest {
public:
    static bool run(const QString &preamble, const QString &options, const QString &engine) {
        compileservice compiler;
        compiler.set_compiler_command(engine);
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        bool passed = false;
        QObject::connect(&compiler, &compileservice::output_text, [](const QString &text) {
            if (text.contains("Error") || text.contains("!")) qWarning().noquote() << text;
        });
        QObject::connect(&compiler, &compileservice::compile_finished,
                         [&](bool success, const QString &path, const QString &) {
            if (success) {
                pdfcanvas canvas;
                passed = canvas.load_pdf(path) && canvas.page_calibration_valid_;
                if (!passed) {
                    for (const QString suffix : {QString("qtikz"), QString("aux")}) {
                        QFile data(QFileInfo(path).absolutePath() + "/document." + suffix);
                        if (data.open(QIODevice::ReadOnly))
                            qWarning().noquote() << suffix << data.readAll();
                    }
                }
                if (passed) {
                    const QSize size(1400, 1400);
                    const QImage image = canvas.pdf_document_.render(0, size);
                    canvas.update_calibration(QRect(QPoint(0, 0), size));
                    // Compare the exported mapping with actual black ink in the PDF.
                    for (const QPointF world : {QPointF(2, 3), QPointF(3, 3), QPointF(2, 4)}) {
                        const QPointF expected = canvas.world_to_screen(world.x(), world.y());
                        QPointF sum;
                        int count = 0;
                        for (int y = qRound(expected.y()) - 12; y <= qRound(expected.y()) + 12; ++y) {
                            for (int x = qRound(expected.x()) - 12; x <= qRound(expected.x()) + 12; ++x) {
                                if (!image.rect().contains(x, y)) continue;
                                const QColor c = image.pixelColor(x, y);
                                if (c.red() < 50 && c.green() < 50 && c.blue() < 50) {
                                    sum += QPointF(x + 0.5, y + 0.5);
                                    ++count;
                                }
                            }
                        }
                        const double error = count ? QLineF(expected, sum / count).length() : 999;
                        // Allow raster quantization/antialiasing at the pixel boundary.
                        if (error > 1.5) {
                            qWarning() << "Mapping error" << world << expected << error;
                            passed = false;
                        }
                        QPointF roundtrip;
                        if (!canvas.screen_to_world(expected, roundtrip) ||
                            QLineF(world, roundtrip).length() > 1e-8) passed = false;
                    }
                    // Pan and resize must preserve the same document coordinates.
                    canvas.update_calibration(QRect(37, -21, 700, 900));
                    QPointF roundtrip;
                    if (!canvas.screen_to_world(canvas.world_to_screen(2, 3), roundtrip) ||
                        QLineF(roundtrip, QPointF(2, 3)).length() > 1e-8) passed = false;
                    QFile::remove(QFileInfo(path).absolutePath() + "/document.qtikz");
                    canvas.load_pdf(path);
                    if (canvas.page_calibration_valid_ || canvas.calibration_valid_) passed = false;
                }
            }
            loop.quit();
        });
        const QString source = preamble + "\n\\usepackage{tikz}\n\\begin{document}\n"
            "\\begin{tikzpicture}[" + options + "]\n"
            "\\path[use as bounding box] (-2,-1) rectangle (7,7);\n"
            "\\fill (2,3) circle[radius=2pt];\n"
            "\\fill (3,3) circle[radius=2pt];\n"
            "\\fill (2,4) circle[radius=2pt];\n"
            "\\fill[magenta] (0,0) circle[radius=3pt];\n"
            "\\fill[cyan] (1,0) circle[radius=3pt];\n"
            "\\fill[blue] (0,1) circle[radius=3pt];\n"
            "\\end{tikzpicture}\n\\end{document}\n";
        timeout.start(30000);
        compiler.compile(source, 0, 20);
        loop.exec();
        if (compiler.is_busy()) compiler.cancel();
        qInfo() << engine << preamble << options << (passed ? "PASS" : "FAIL");
        return passed;
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    bool passed = true;
    for (const QString engine : {QString("pdflatex"), QString("lualatex")}) {
        for (const QString preamble : {QString("\\documentclass{article}"),
                                       QString("\\documentclass[border=7pt]{standalone}")}) {
            for (const QString options : {QString(), QString("rotate=23,xscale=1.3,yscale=0.7"),
                                          QString("x={(1cm,0.2cm)},y={(0.3cm,1cm)}")}) {
                passed = calibrationtest::run(preamble, options, engine) && passed;
            }
        }
    }
    return passed ? 0 : 1;
}
