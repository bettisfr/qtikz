#include "compileservice.h"

#include <QDateTime>
#include <QFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

compileservice::compileservice(QObject *parent) : QObject(parent) {
    connect(&proc_, &QProcess::readyReadStandardOutput, this, &compileservice::on_ready_output);
    connect(&proc_, &QProcess::readyReadStandardError, this, &compileservice::on_ready_output);
    connect(&proc_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &compileservice::on_finished);
}

bool compileservice::is_busy() const {
    return proc_.state() != QProcess::NotRunning;
}

void compileservice::set_compiler_command(const QString &command) {
    const QString trimmed = command.trimmed();
    compiler_command_ = trimmed.isEmpty() ? QStringLiteral("pdflatex") : trimmed;
}

QString compileservice::compiler_command() const {
    return compiler_command_;
}

void compileservice::cancel() {
    if (!is_busy()) {
        return;
    }
    canceled_ = true;
    proc_.kill();
}

bool compileservice::ensure_work_dir() {
    if (!work_dir_path_.isEmpty()) {
        return true;
    }
    temp_dir_ = std::make_unique<QTemporaryDir>();
    if (!temp_dir_->isValid()) {
        return false;
    }
    work_dir_path_ = temp_dir_->path();
    return true;
}

QString compileservice::format_step(double step) {
    QString s = QString::number(step, 'f', 4);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
        s.chop(1);
    }
    return s;
}

QString compileservice::inject_grid(const QString &source, int grid_step_mm, int grid_extent_cm) {
    static const QRegularExpression begin_tikz_pattern(R"(\\begin\{tikzpicture\}(?:\[[^\]]*\])?)");
    static const QRegularExpression end_tikz_pattern(R"(\\end\{tikzpicture\})");

    const QRegularExpressionMatch begin_match = begin_tikz_pattern.match(source);
    const QRegularExpressionMatch end_match = end_tikz_pattern.match(source);
    if (!begin_match.hasMatch() || !end_match.hasMatch()) {
        return source;
    }

    const bool draw_grid = grid_step_mm > 0;
    const QString step_expr = draw_grid ? format_step(static_cast<double>(grid_step_mm) / 10.0) : QStringLiteral("1");
    const int clamped_extent = qBound(20, grid_extent_cm, 100);
    const double half = static_cast<double>(clamped_extent) / 2.0;
    const QString min_xy = format_step(-half);
    const QString max_xy = format_step(half);

    QString grid_block = "\n  % ktikz preview grid\n";
    if (draw_grid) {
        if (grid_step_mm != 10) {
            grid_block += "  \\draw[step=" + step_expr + ", gray!18, very thin] (" + min_xy + "," + min_xy + ") grid (" + max_xy + "," + max_xy + ");\n";
        }
        grid_block += "  \\draw[step=1, gray!38, thin] (" + min_xy + "," + min_xy + ") grid (" + max_xy + "," + max_xy + ");\n";
        grid_block += "  \\draw[gray!50, thin] (" + min_xy + ",0) -- (" + max_xy + ",0);\n";
        grid_block += "  \\draw[gray!50, thin] (0," + min_xy + ") -- (0," + max_xy + ");\n";
    }

    // PGF writes the picture's absolute origin to the aux file at shipout.
    // Export the local basis in scaled TeX points before drawing the contents.
    // No paths or nodes are added, so calibration cannot change the bounding box.
    const QString calibration_block = QString::fromLatin1(R"TEX(
\pgfrememberpicturepositiononpagetrue
\newwrite\qtikzpositions
\immediate\openout\qtikzpositions=\jobname.qtikz
\immediate\write\qtikzpositions{QTIKZ1 \pgfpictureid}
\begingroup
\def\qtikzpoint#1#2{%
  \csname pgf@process\endcsname{\pgfpointtransformed{\pgfpointxy{#1}{#2}}}%
  \immediate\write\qtikzpositions{\number\csname pgf@x\endcsname\space\number\csname pgf@y\endcsname}}
\qtikzpoint{0}{0}
\qtikzpoint{1}{0}
\qtikzpoint{0}{1}
\endgroup
\write\qtikzpositions{PAGE \number\ReadonlyShipoutCounter}
)TEX");

    QString out = source;
    out.insert(begin_match.capturedEnd(0), calibration_block + grid_block);
    return out;
}

void compileservice::compile(const QString &source_text, int grid_step_mm, int grid_extent_cm) {
    if (is_busy()) {
        emit output_text("[Compile] Already running");
        return;
    }
    canceled_ = false;

    if (!ensure_work_dir()) {
        emit output_text("[Compile] Could not create temporary directory");
        emit compile_finished(false, QString(), "workdir creation failed");
        return;
    }

    // Never pair a new PDF with calibration left by an earlier compilation.
    QFile::remove(work_dir_path_ + "/document.qtikz");
    QFile tex_file(work_dir_path_ + "/document.tex");
    if (!tex_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit output_text("[Compile] Could not write document.tex");
        emit compile_finished(false, QString(), "write failed");
        return;
    }

    QTextStream out(&tex_file);
    out << inject_grid(source_text, grid_step_mm, grid_extent_cm);
    tex_file.close();

    emit output_text("\n[Compile] " + QDateTime::currentDateTime().toString(Qt::ISODate));
    emit output_text("[Compile] Running " + compiler_command_ + "...");

    proc_.setWorkingDirectory(work_dir_path_);
    proc_.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    QStringList command_parts = QProcess::splitCommand(compiler_command_);
    QString program = QStringLiteral("pdflatex");
    if (!command_parts.isEmpty()) {
        program = command_parts.takeFirst();
    }
    command_parts << "-interaction=nonstopmode" << "-halt-on-error" << "-file-line-error" << "document.tex";
    proc_.start(program, command_parts);
    if (!proc_.waitForStarted(1500)) {
        emit output_text("[Error] Unable to start compiler: " + compiler_command_);
        emit compile_finished(false, QString(), "start failed");
    }
}

void compileservice::on_ready_output() {
    const QByteArray std_out = proc_.readAllStandardOutput();
    const QByteArray std_err = proc_.readAllStandardError();
    if (!std_out.isEmpty()) {
        emit output_text(QString::fromLocal8Bit(std_out));
    }
    if (!std_err.isEmpty()) {
        emit output_text(QString::fromLocal8Bit(std_err));
    }
}

void compileservice::on_finished(int exit_code, QProcess::ExitStatus status) {
    if (canceled_) {
        canceled_ = false;
        emit output_text("[Compile] Canceled");
        emit compile_finished(false, QString(), "canceled");
        return;
    }

    if (status != QProcess::NormalExit || exit_code != 0) {
        emit output_text("[Compile] Failed");
        emit compile_finished(false, QString(), "compile failed");
        return;
    }

    const QString pdf_path = work_dir_path_ + "/document.pdf";
    emit output_text("[Preview] PDF updated (with injected grid)");
    emit compile_finished(true, pdf_path, "ok");
}
