#include "mainwindow.h"

#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextCharFormat>
#include <QSyntaxHighlighter>
#include <QTimer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QStyleFactory>

#include "compileservice.h"
#include "coordinateparser.h"
#include "pdfcanvas.h"
#include "appconfig.h"
#include "settingsdialog.h"

namespace {
class linenumberedit;
class verticaltoolbutton : public QToolButton {
public:
    explicit verticaltoolbutton(QWidget *parent = nullptr) : QToolButton(parent) {}

    QSize sizeHint() const override {
        const QSize s = QToolButton::sizeHint();
        return QSize(qMax(24, s.height() + 10), qMax(90, s.width() + 10));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionToolButton opt;
        initStyleOption(&opt);

        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_PanelButtonTool, &opt, &painter, this);

        painter.save();
        painter.translate(width() * 0.5, height() * 0.5);
        painter.rotate(-90);
        const QRect text_rect(-height() / 2, -width() / 2, height(), width());
        painter.setPen(opt.palette.buttonText().color());
        painter.drawText(text_rect, Qt::AlignCenter, text());
        painter.restore();
    }
};

class linenumberarea : public QWidget {
public:
    explicit linenumberarea(QWidget *parent, linenumberedit *editor) : QWidget(parent), editor_(editor) {}
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    linenumberedit *editor_;
};

class linenumberedit : public QPlainTextEdit {
public:
    explicit linenumberedit(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
        line_number_area_ = new linenumberarea(this, this);
        connect(this, &QPlainTextEdit::blockCountChanged, this, &linenumberedit::update_line_number_area_width);
        connect(this, &QPlainTextEdit::updateRequest, this, &linenumberedit::update_line_number_area);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() { line_number_area_->update(); });
        update_line_number_area_width(0);
    }

    int line_number_area_width() const {
        if (!show_line_numbers_) {
            return 0;
        }
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void line_number_area_paint_event(QPaintEvent *event) {
        if (!show_line_numbers_) {
            return;
        }
        QPainter painter(line_number_area_);
        painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));

        QTextBlock block = firstVisibleBlock();
        int block_number = block.blockNumber();
        int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + static_cast<int>(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(block_number + 1);
                const bool current = (textCursor().blockNumber() == block_number);
                painter.setPen(current ? palette().color(QPalette::Text) : palette().color(QPalette::Mid));
                painter.drawText(0,
                                 top,
                                 line_number_area_->width() - 4,
                                 fontMetrics().height(),
                                 Qt::AlignRight,
                                 number);
            }

            block = block.next();
            top = bottom;
            bottom = top + static_cast<int>(blockBoundingRect(block).height());
            ++block_number;
        }
    }

    void set_line_numbers_visible(bool visible) {
        show_line_numbers_ = visible;
        line_number_area_->setVisible(show_line_numbers_);
        update_line_number_area_width(0);
        viewport()->update();
    }

    bool line_numbers_visible() const {
        return show_line_numbers_;
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect cr = contentsRect();
        line_number_area_->setGeometry(QRect(cr.left(), cr.top(), line_number_area_width(), cr.height()));
    }

private:
    void update_line_number_area_width(int) {
        setViewportMargins(line_number_area_width(), 0, 0, 0);
    }

    void update_line_number_area(const QRect &rect, int dy) {
        if (dy) {
            line_number_area_->scroll(0, dy);
        } else {
            line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
        }

        if (rect.contains(viewport()->rect())) {
            update_line_number_area_width(0);
        }
    }

    linenumberarea *line_number_area_ = nullptr;
    bool show_line_numbers_ = true;
};

QSize linenumberarea::sizeHint() const {
    return QSize(editor_->line_number_area_width(), 0);
}

void linenumberarea::paintEvent(QPaintEvent *event) {
    editor_->line_number_area_paint_event(event);
}

class latexhighlighter : public QSyntaxHighlighter {
public:
    explicit latexhighlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {
        QTextCharFormat command_fmt;
        command_fmt.setForeground(QColor("#1d4ed8"));
        command_fmt.setFontWeight(QFont::Bold);
        rules_.push_back({QRegularExpression(R"(\\[A-Za-z@]+)"), command_fmt});

        QTextCharFormat env_fmt;
        env_fmt.setForeground(QColor("#7c3aed"));
        rules_.push_back({QRegularExpression(R"(\\(begin|end)\{[^}]+\})"), env_fmt});

        QTextCharFormat number_fmt;
        number_fmt.setForeground(QColor("#059669"));
        rules_.push_back({QRegularExpression(R"([+-]?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)"), number_fmt});

        QTextCharFormat comment_fmt;
        comment_fmt.setForeground(QColor("#6b7280"));
        rules_.push_back({QRegularExpression(R"(%[^\n]*)"), comment_fmt});
    }

protected:
    void highlightBlock(const QString &text) override {
        for (const rule &r : rules_) {
            QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), r.format);
            }
        }
    }

private:
    struct rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    std::vector<rule> rules_;
};

QString wrap_tikz_document(const QString &tikz_body) {
    return "\\documentclass[tikz,border=10pt]{standalone}\n"
           "\\usepackage{tikz}\n"
           "\\begin{document}\n"
           "\\begin{tikzpicture}\n" + tikz_body +
           "\\end{tikzpicture}\n"
           "\\end{document}\n";
}

QString minimal_tikz_document_text() {
    return "\\documentclass[tikz]{standalone}\n"
           "\\usepackage{tikz}\n"
           "\\begin{document}\n"
           "\\begin{tikzpicture}\n"
           "\n"
           "\\end{tikzpicture}\n"
           "\\end{document}\n";
}

void append_colored_log(QTextEdit *output, const QString &text, const QColor &color) {
    if (!output) {
        return;
    }
    QTextCursor cursor(output->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(color);
    cursor.insertText(text, fmt);
    if (!text.endsWith('\n')) {
        cursor.insertText("\n", fmt);
    }
    output->setTextCursor(cursor);
    output->ensureCursorVisible();
}

QPalette make_dark_palette() {
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(45, 45, 45));
    palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    palette.setColor(QPalette::Text, QColor(230, 230, 230));
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    return palette;
}
} // namespace

mainwindow::mainwindow(QWidget *parent) : QMainWindow(parent) {
    update_window_title();
    resize(1200, 800);

    QFont editor_font;
    editor_font.setFamily(editor_font_family_);
    editor_font.setStyleHint(QFont::Monospace);
    editor_font.setPointSize(editor_font_size_);
    editor_ = new linenumberedit(this);
    editor_->setFont(editor_font);
    editor_->setTabStopDistance(editor_->fontMetrics().horizontalAdvance(QStringLiteral(" ")) * 4);
    editor_->setPlainText(minimal_tikz_document_text());
    editor_->document()->setModified(false);
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &mainwindow::on_document_modified_changed);
    connect(editor_, &QPlainTextEdit::textChanged, this, &mainwindow::on_editor_text_changed);
    new latexhighlighter(editor_->document());
    update_window_title();

    preview_canvas_ = new pdfcanvas(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *right_splitter = new QSplitter(Qt::Horizontal, splitter);
    splitter->addWidget(editor_);

    auto *right_pane = new QWidget(this);
    right_pane->setObjectName("previewRightPane");
    auto *right_layout = new QVBoxLayout(right_pane);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(6);
    right_layout->addWidget(preview_canvas_, 1);

    auto *controls_row = new QWidget(right_pane);
    controls_row->setObjectName("previewControlsRow");
    auto *controls_layout = new QHBoxLayout(controls_row);
    controls_layout->setContentsMargins(6, 0, 6, 6);
    controls_layout->setSpacing(8);

    auto *step_label = new QLabel("Grid/Snap:", controls_row);
    grid_step_combo_ = new QComboBox(controls_row);
    grid_step_combo_->addItem("10 mm", 10);
    grid_step_combo_->addItem("5 mm", 5);
    grid_step_combo_->addItem("2 mm", 2);
    grid_step_combo_->addItem("1 mm", 1);
    grid_step_combo_->addItem("0 (free)", 0);
    grid_step_combo_->setCurrentIndex(0);
    auto *extent_label = new QLabel("Extent:", controls_row);
    grid_extent_spin_ = new QSpinBox(controls_row);
    grid_extent_spin_->setRange(20, 100);
    grid_extent_spin_->setSingleStep(5);
    grid_extent_spin_->setSuffix(" cm");
    grid_extent_spin_->setValue(grid_extent_cm_);

    controls_layout->addWidget(step_label);
    controls_layout->addWidget(grid_step_combo_);
    controls_layout->addWidget(extent_label);
    controls_layout->addWidget(grid_extent_spin_);
    controls_layout->addStretch(1);

    right_layout->addWidget(controls_row, 0);

    auto *properties_scroll = new QScrollArea(right_splitter);
    properties_scroll->setWidgetResizable(true);
    properties_scroll->setFrameShape(QFrame::NoFrame);
    properties_scroll->setMinimumWidth(260);
    auto *properties_pane = new QWidget(properties_scroll);
    properties_pane->setObjectName("propertiesPane");
    auto *properties_layout = new QVBoxLayout(properties_pane);
    properties_layout->setContentsMargins(8, 8, 8, 8);
    properties_layout->setSpacing(6);
    auto *properties_title = new QLabel("Properties", properties_pane);
    QFont title_font = properties_title->font();
    title_font.setBold(true);
    properties_title->setFont(title_font);
    auto *selection_label = new QLabel("Selected Object", properties_pane);
    QFont selection_title_font = selection_label->font();
    selection_title_font.setBold(true);
    selection_label->setFont(selection_title_font);
    props_selection_value_ = new QLabel("None", properties_pane);
    props_selection_value_->setObjectName("selectionPill");
    props_selection_value_->setAlignment(Qt::AlignCenter);
    props_selection_value_->setStyleSheet(
        "padding:4px 8px; border:1px solid #cbd5e1; border-radius:8px; background:#f8fafc; color:#0f172a;");

    auto mk_spin = [properties_pane]() {
        auto *s = new QDoubleSpinBox(properties_pane);
        s->setRange(-1000000.0, 1000000.0);
        s->setDecimals(4);
        s->setSingleStep(0.1);
        return s;
    };
    props_label_1_ = new QLabel("Value 1", properties_pane);
    props_label_2_ = new QLabel("Value 2", properties_pane);
    props_label_3_ = new QLabel("Value 3", properties_pane);
    props_label_4_ = new QLabel("Value 4", properties_pane);
    props_value_1_ = mk_spin();
    props_value_2_ = mk_spin();
    props_value_3_ = mk_spin();
    props_value_4_ = mk_spin();
    connect(props_value_1_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    connect(props_value_2_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    connect(props_value_3_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    connect(props_value_4_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        apply_selected_geometry_changes();
    });
    auto *numeric_form = new QFormLayout;
    numeric_form->setContentsMargins(0, 0, 0, 0);
    numeric_form->addRow(props_label_1_, props_value_1_);
    numeric_form->addRow(props_label_2_, props_value_2_);
    numeric_form->addRow(props_label_3_, props_value_3_);
    numeric_form->addRow(props_label_4_, props_value_4_);
    auto *geometry_box = new QGroupBox("Geometry", properties_pane);
    auto *geometry_box_layout = new QVBoxLayout(geometry_box);
    geometry_box_layout->setContentsMargins(8, 8, 8, 8);
    geometry_box_layout->addLayout(numeric_form);

    props_color_combo_ = new QComboBox(properties_pane);
    props_color_combo_->addItems({"black", "blue", "red", "green", "orange", "magenta", "brown", "cyan", "gray", "yellow"});
    props_endpoint_start_combo_ = new QComboBox(properties_pane);
    props_endpoint_start_combo_->addItems({"none", "arrow", "bar"});
    props_endpoint_end_combo_ = new QComboBox(properties_pane);
    props_endpoint_end_combo_->addItems({"none", "arrow", "bar"});
    props_line_style_combo_ = new QComboBox(properties_pane);
    props_line_style_combo_->addItems({"solid",
                                       "dashed",
                                       "densely dashed",
                                       "loosely dashed",
                                       "dotted",
                                       "densely dotted",
                                       "loosely dotted",
                                       "dashdotted",
                                       "densely dashdotted",
                                       "loosely dashdotted"});
    props_thickness_combo_ = new QComboBox(properties_pane);
    props_thickness_combo_->addItems({"thin", "semithick", "thick", "very thick", "ultra thick"});
    props_draw_opacity_combo_ = new QComboBox(properties_pane);
    for (int i = 1; i <= 10; ++i) {
        const double v = static_cast<double>(i) / 10.0;
        props_draw_opacity_combo_->addItem(QString::number(v, 'f', 1), v);
    }
    props_fill_color_combo_ = new QComboBox(properties_pane);
    props_fill_color_combo_->addItems({"none", "black", "blue", "red", "green", "orange", "magenta", "brown", "cyan", "gray", "yellow"});
    props_fill_opacity_combo_ = new QComboBox(properties_pane);
    for (int i = 1; i <= 10; ++i) {
        const double v = static_cast<double>(i) / 10.0;
        props_fill_opacity_combo_->addItem(QString::number(v, 'f', 1), v);
    }
    connect(props_color_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_endpoint_start_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_endpoint_end_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_line_style_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_thickness_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_draw_opacity_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_fill_color_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    connect(props_fill_opacity_combo_, &QComboBox::currentTextChanged, this, [this](const QString &) {
        apply_selected_style_changes();
    });
    auto *border_form = new QFormLayout;
    border_form->setContentsMargins(0, 0, 0, 0);
    border_form->addRow("Color", props_color_combo_);
    border_form->addRow("Endpoint start", props_endpoint_start_combo_);
    border_form->addRow("Endpoint end", props_endpoint_end_combo_);
    border_form->addRow("Style", props_line_style_combo_);
    border_form->addRow("Thickness", props_thickness_combo_);
    border_form->addRow("Opacity", props_draw_opacity_combo_);
    auto *border_box = new QGroupBox("Border", properties_pane);
    auto *border_box_layout = new QVBoxLayout(border_box);
    border_box_layout->setContentsMargins(8, 8, 8, 8);
    border_box_layout->addLayout(border_form);

    auto *fill_form = new QFormLayout;
    fill_form->setContentsMargins(0, 0, 0, 0);
    fill_form->addRow("Color", props_fill_color_combo_);
    fill_form->addRow("Opacity", props_fill_opacity_combo_);
    auto *fill_box = new QGroupBox("Fill", properties_pane);
    auto *fill_box_layout = new QVBoxLayout(fill_box);
    fill_box_layout->setContentsMargins(8, 8, 8, 8);
    fill_box_layout->addLayout(fill_form);
    props_delete_btn_ = new QPushButton("Delete Selected", properties_pane);
    props_delete_btn_->setEnabled(false);
    connect(props_delete_btn_, &QPushButton::clicked, this, &mainwindow::delete_selected_object);
    properties_layout->addWidget(properties_title);
    properties_layout->addWidget(selection_label);
    properties_layout->addWidget(props_selection_value_);
    properties_layout->addWidget(geometry_box);
    properties_layout->addWidget(border_box);
    properties_layout->addWidget(fill_box);
    properties_layout->addWidget(props_delete_btn_);
    properties_layout->addStretch(1);
    properties_scroll->setWidget(properties_pane);

    right_splitter->addWidget(right_pane);
    right_splitter->addWidget(properties_scroll);
    right_splitter->setStretchFactor(0, 4);
    right_splitter->setStretchFactor(1, 1);
    right_splitter->setSizes({780, 250});

    splitter->addWidget(right_splitter);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({560, 840});

    left_main_splitter_ = new QSplitter(Qt::Horizontal, this);
    left_panel_ = new QFrame(left_main_splitter_);
    left_panel_->setObjectName("leftObjectsPanel");
    left_panel_->setMinimumWidth(140);
    auto *left_layout = new QVBoxLayout(left_panel_);
    left_layout->setContentsMargins(8, 8, 8, 8);
    left_layout->setSpacing(6);
    auto *left_title = new QLabel("Objects", left_panel_);
    QFont left_title_font = left_title->font();
    left_title_font.setBold(true);
    left_title->setFont(left_title_font);
    left_layout->addWidget(left_title);
    left_line_button_ = new QPushButton("Line", left_panel_);
    left_layout->addWidget(left_line_button_);
    left_polyline_button_ = new QPushButton("Polyline", left_panel_);
    left_layout->addWidget(left_polyline_button_);
    left_circle_button_ = new QPushButton("Circle", left_panel_);
    left_layout->addWidget(left_circle_button_);
    left_ellipse_button_ = new QPushButton("Ellipse", left_panel_);
    left_layout->addWidget(left_ellipse_button_);
    left_rectangle_button_ = new QPushButton("Rectangle", left_panel_);
    left_layout->addWidget(left_rectangle_button_);
    left_bezier_button_ = new QPushButton("Bezier", left_panel_);
    left_layout->addWidget(left_bezier_button_);
    left_node_button_ = new QPushButton("Text", left_panel_);
    left_layout->addWidget(left_node_button_);
    left_layout->addStretch(1);

    left_main_splitter_->addWidget(left_panel_);
    left_main_splitter_->addWidget(splitter);
    left_main_splitter_->setStretchFactor(0, 0);
    left_main_splitter_->setStretchFactor(1, 1);
    left_main_splitter_->setSizes({0, 1400});

    auto *left_tool_stripe = new QFrame(this);
    left_tool_stripe->setObjectName("leftToolStripe");
    left_tool_stripe->setFixedWidth(28);
    auto *left_tool_layout = new QVBoxLayout(left_tool_stripe);
    left_tool_layout->setContentsMargins(2, 4, 2, 4);
    left_tool_layout->setSpacing(4);
    left_panel_toggle_button_ = new verticaltoolbutton(left_tool_stripe);
    left_panel_toggle_button_->setText("Add object");
    left_panel_toggle_button_->setCheckable(true);
    left_panel_toggle_button_->setToolTip("Toggle Objects Panel");
    connect(left_panel_toggle_button_, &QToolButton::clicked, this, &mainwindow::toggle_left_panel);
    left_tool_layout->addWidget(left_panel_toggle_button_);
    left_tool_layout->addStretch(1);

    auto *left_container = new QWidget(this);
    auto *left_container_layout = new QHBoxLayout(left_container);
    left_container_layout->setContentsMargins(0, 0, 0, 0);
    left_container_layout->setSpacing(0);
    left_container_layout->addWidget(left_tool_stripe);
    left_container_layout->addWidget(left_main_splitter_, 1);

    output_ = new QTextEdit(this);
    output_->setReadOnly(true);
    output_->setPlaceholderText("Compilation output will appear here...");
    auto *output_section = new QWidget(this);
    auto *output_layout = new QVBoxLayout(output_section);
    output_layout->setContentsMargins(6, 4, 6, 6);
    output_layout->setSpacing(4);
    auto *output_header = new QWidget(output_section);
    auto *output_header_layout = new QHBoxLayout(output_header);
    output_header_layout->setContentsMargins(0, 0, 0, 0);
    output_header_layout->setSpacing(6);
    auto *output_label = new QLabel("Console output", output_header);
    QFont output_label_font = output_label->font();
    output_label_font.setBold(true);
    output_label->setFont(output_label_font);
    auto *clear_log_btn = new QPushButton("Clear", output_header);
    connect(clear_log_btn, &QPushButton::clicked, this, [this]() {
        if (output_) {
            output_->clear();
        }
    });
    output_header_layout->addWidget(output_label);
    output_header_layout->addStretch(1);
    output_header_layout->addWidget(clear_log_btn);
    output_layout->addWidget(output_header);
    output_layout->addWidget(output_, 1);

    auto *main_splitter = new QSplitter(Qt::Vertical, this);
    main_splitter->addWidget(left_container);
    main_splitter->addWidget(output_section);
    main_splitter->setSizes({650, 150});

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(main_splitter);
    setCentralWidget(central);

    compile_service_ = new compileservice(this);

    auto_compile_timer_ = new QTimer(this);
    auto_compile_timer_->setSingleShot(true);
    auto_compile_timer_->setInterval(auto_compile_delay_ms_);
    connect(auto_compile_timer_, &QTimer::timeout, this, &mainwindow::on_auto_compile_timeout);

    connect(preview_canvas_, &pdfcanvas::coordinate_dragged, this, &mainwindow::on_coordinate_dragged);
    connect(preview_canvas_, &pdfcanvas::circle_radius_dragged, this, &mainwindow::on_circle_radius_dragged);
    connect(preview_canvas_, &pdfcanvas::ellipse_radii_dragged, this, &mainwindow::on_ellipse_radii_dragged);
    connect(preview_canvas_, &pdfcanvas::bezier_control_dragged, this, &mainwindow::on_bezier_control_dragged);
    connect(preview_canvas_, &pdfcanvas::rectangle_corner_dragged, this, &mainwindow::on_rectangle_corner_dragged);
    connect(preview_canvas_, &pdfcanvas::selection_changed, this, &mainwindow::on_canvas_selection_changed);
    connect(preview_canvas_, &pdfcanvas::add_point_clicked, this, &mainwindow::on_canvas_add_point);
    connect(grid_step_combo_, &QComboBox::currentIndexChanged, this, &mainwindow::on_grid_step_changed);
    connect(grid_extent_spin_, &QSpinBox::valueChanged, this, &mainwindow::on_grid_extent_changed);

    connect(compile_service_, &compileservice::output_text, this, &mainwindow::on_compile_service_output);
    connect(compile_service_, &compileservice::compile_finished, this, &mainwindow::on_compile_finished);

    preview_canvas_->set_snap_mm(grid_snap_mm_);
    load_settings();

    create_menu_and_toolbar();
    clear_properties_panel();
    statusBar()->showMessage("Ready");
    connect(left_line_button_, &QPushButton::clicked, this, &mainwindow::start_add_line_mode);
    connect(left_polyline_button_, &QPushButton::clicked, this, &mainwindow::start_add_polyline_mode);
    connect(left_circle_button_, &QPushButton::clicked, this, &mainwindow::start_add_circle_mode);
    connect(left_rectangle_button_, &QPushButton::clicked, this, &mainwindow::start_add_rectangle_mode);
    connect(left_ellipse_button_, &QPushButton::clicked, this, &mainwindow::start_add_ellipse_mode);
    connect(left_bezier_button_, &QPushButton::clicked, this, &mainwindow::start_add_bezier_mode);
    connect(left_node_button_, &QPushButton::clicked, this, &mainwindow::start_add_node_mode);
}

void mainwindow::closeEvent(QCloseEvent *event) {
    if (maybe_save_before_action("Quit", "Save changes before quitting?")) {
        event->accept();
    } else {
        event->ignore();
    }
}

QString mainwindow::minimal_tikz_document() {
    return minimal_tikz_document_text();
}

void mainwindow::update_window_title() {
    const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
    const QString file_part =
        current_file_path_.isEmpty() ? QStringLiteral("untitled") : QFileInfo(current_file_path_).fileName();
    const bool modified = editor_ && editor_->document()->isModified();
    setWindowTitle(app_name + " - " + file_part + (modified ? "*" : ""));
}

bool mainwindow::maybe_save_before_action(const QString &title, const QString &text) {
    if (!editor_ || !editor_->document()->isModified()) {
        return true;
    }

    QMessageBox box(this);
    box.setWindowTitle(title);
    box.setText(text);
    box.setIcon(QMessageBox::Warning);
    QAbstractButton *save_btn = box.addButton("Save", QMessageBox::AcceptRole);
    QAbstractButton *discard_btn = box.addButton("Discard", QMessageBox::DestructiveRole);
    QAbstractButton *cancel_btn = box.addButton("Cancel", QMessageBox::RejectRole);
    box.setDefaultButton(static_cast<QPushButton *>(save_btn));
    box.exec();

    if (box.clickedButton() == cancel_btn) {
        return false;
    }
    if (box.clickedButton() == discard_btn) {
        return true;
    }
    save_file();
    return !editor_->document()->isModified();
}

void mainwindow::request_compile(bool cancel_running) {
    if (!compile_service_ || !editor_) {
        return;
    }
    if (compile_service_->is_busy()) {
        pending_compile_ = true;
        if (cancel_running) {
            compile_service_->cancel();
        }
        return;
    }

    const QString source_text = editor_->toPlainText();
    coordinate_refs_ = coordinateparser::extract_refs(source_text);
    circle_refs_ = coordinateparser::extract_circle_refs(source_text);
    ellipse_refs_ = coordinateparser::extract_ellipse_refs(source_text);
    bezier_refs_ = coordinateparser::extract_bezier_refs(source_text);
    rectangle_refs_ = coordinateparser::extract_rectangle_refs(source_text);
    update_properties_panel();
    preview_canvas_->set_coordinates(coordinateparser::extract_pairs(source_text));
    preview_canvas_->set_circles(coordinateparser::extract_circle_pairs(source_text));
    preview_canvas_->set_ellipses(coordinateparser::extract_ellipse_pairs(source_text));
    preview_canvas_->set_beziers(coordinateparser::extract_bezier_pairs(source_text));
    preview_canvas_->set_rectangles(coordinateparser::extract_rectangle_pairs(source_text));
    compile_service_->compile(source_text, grid_display_mm_, grid_extent_cm_);
    statusBar()->showMessage("Compiling...");
}

void mainwindow::replace_editor_text_preserve_undo(const QString &text) {
    suppress_auto_compile_ = true;
    editor_->setPlainText(text);
    suppress_auto_compile_ = false;
}

void mainwindow::on_editor_text_changed() {
    update_properties_panel();
    if (suppress_auto_compile_) {
        return;
    }
    if (auto_compile_timer_) {
        auto_compile_timer_->start();
    }
}

void mainwindow::on_auto_compile_timeout() {
    request_compile(true);
}

void mainwindow::toggle_left_panel() {
    if (!left_main_splitter_ || !left_panel_) {
        return;
    }
    const QList<int> sizes = left_main_splitter_->sizes();
    const bool currently_hidden = sizes.isEmpty() || sizes[0] < 20;
    if (currently_hidden) {
        left_main_splitter_->setSizes({170, qMax(400, width() - 170)});
        if (left_panel_toggle_button_) {
            left_panel_toggle_button_->setChecked(true);
        }
    } else {
        left_main_splitter_->setSizes({0, qMax(400, width())});
        if (left_panel_toggle_button_) {
            left_panel_toggle_button_->setChecked(false);
        }
    }
}

void mainwindow::set_add_object_mode(const QString &mode) {
    add_object_mode_ = mode;
    const bool enabled = !add_object_mode_.isEmpty();
    if (preview_canvas_) {
        preview_canvas_->set_add_line_mode(enabled);
        if (enabled) {
            preview_canvas_->setCursor(Qt::CrossCursor);
        } else {
            preview_canvas_->unsetCursor();
        }
    }
    if (left_line_button_) {
        left_line_button_->setDown(add_object_mode_ == "line");
    }
    if (left_polyline_button_) {
        left_polyline_button_->setDown(add_object_mode_ == "polyline");
    }
    if (left_circle_button_) {
        left_circle_button_->setDown(add_object_mode_ == "circle");
    }
    if (left_rectangle_button_) {
        left_rectangle_button_->setDown(add_object_mode_ == "rectangle");
    }
    if (left_ellipse_button_) {
        left_ellipse_button_->setDown(add_object_mode_ == "ellipse");
    }
    if (left_bezier_button_) {
        left_bezier_button_->setDown(add_object_mode_ == "bezier");
    }
    if (left_node_button_) {
        left_node_button_->setDown(add_object_mode_ == "node");
    }
}

void mainwindow::start_add_line_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "line" ? QString() : QStringLiteral("line"));
    if (add_object_mode_ == "line") {
        statusBar()->showMessage("Add Line: click in preview to place line", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::start_add_polyline_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "polyline" ? QString() : QStringLiteral("polyline"));
    if (add_object_mode_ == "polyline") {
        statusBar()->showMessage("Add Polyline: click in preview to place", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::start_add_circle_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "circle" ? QString() : QStringLiteral("circle"));
    if (add_object_mode_ == "circle") {
        statusBar()->showMessage("Add Circle: click in preview to place", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::start_add_rectangle_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "rectangle" ? QString() : QStringLiteral("rectangle"));
    if (add_object_mode_ == "rectangle") {
        statusBar()->showMessage("Add Rectangle: click in preview to place", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::start_add_ellipse_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "ellipse" ? QString() : QStringLiteral("ellipse"));
    if (add_object_mode_ == "ellipse") {
        statusBar()->showMessage("Add Ellipse: click in preview to place", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::start_add_bezier_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "bezier" ? QString() : QStringLiteral("bezier"));
    if (add_object_mode_ == "bezier") {
        statusBar()->showMessage("Add Bezier: click in preview to place", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::start_add_node_mode() {
    ensure_minimal_document_loaded();
    set_add_object_mode(add_object_mode_ == "node" ? QString() : QStringLiteral("node"));
    if (add_object_mode_ == "node") {
        statusBar()->showMessage("Add Text: click in preview to place", 3000);
    } else {
        statusBar()->showMessage("Add object canceled", 1500);
    }
}

void mainwindow::ensure_minimal_document_loaded() {
    if (!editor_) {
        return;
    }
    if (editor_->toPlainText().trimmed().isEmpty()) {
        replace_editor_text_preserve_undo(minimal_tikz_document());
        editor_->document()->setModified(false);
        update_window_title();
    }
}

void mainwindow::on_canvas_add_point(double x, double y) {
    if (!editor_ || add_object_mode_.isEmpty()) {
        return;
    }
    QString text = editor_->toPlainText();
    if (text.trimmed().isEmpty()) {
        text = minimal_tikz_document();
    }
    const QString end_tag = "\\end{tikzpicture}";
    int pos = text.indexOf(end_tag);
    if (pos < 0) {
        text = minimal_tikz_document();
        pos = text.indexOf(end_tag);
    }
    if (pos < 0) {
        return;
    }

    const QString x0 = coordinateparser::format_number(x);
    const QString y0 = coordinateparser::format_number(y);
    const QString x1 = coordinateparser::format_number(x + 2.0);
    const QString y1 = coordinateparser::format_number(y + 1.0);
    QString cmd;
    if (add_object_mode_ == "line") {
        cmd = "  \\draw (" + x0 + "," + y0 + ") -- (" + x1 + "," + y1 + ");\n";
    } else if (add_object_mode_ == "polyline") {
        const QString x2 = coordinateparser::format_number(x + 1.2);
        const QString y2 = coordinateparser::format_number(y + 1.6);
        const QString x3 = coordinateparser::format_number(x + 2.5);
        const QString y3 = coordinateparser::format_number(y + 0.2);
        const QString x4 = coordinateparser::format_number(x + 3.4);
        const QString y4 = coordinateparser::format_number(y + 1.4);
        cmd = "  \\draw (" + x0 + "," + y0 + ") -- (" + x2 + "," + y2 + ") -- (" + x3 + "," + y3 + ") -- (" + x4 +
              "," + y4 + ");\n";
    } else if (add_object_mode_ == "circle") {
        cmd = "  \\draw (" + x0 + "," + y0 + ") circle (1);\n";
    } else if (add_object_mode_ == "ellipse") {
        cmd = "  \\draw (" + x0 + "," + y0 + ") ellipse (1 and 0.6);\n";
    } else if (add_object_mode_ == "rectangle") {
        const QString x2 = coordinateparser::format_number(x + 2.0);
        const QString y2 = coordinateparser::format_number(y + 1.2);
        cmd = "  \\draw (" + x0 + "," + y0 + ") rectangle (" + x2 + "," + y2 + ");\n";
    } else if (add_object_mode_ == "bezier") {
        const QString x2 = coordinateparser::format_number(x + 1.0);
        const QString y2 = coordinateparser::format_number(y + 1.5);
        const QString x3 = coordinateparser::format_number(x + 2.0);
        const QString y3 = coordinateparser::format_number(y - 1.0);
        const QString x4 = coordinateparser::format_number(x + 3.0);
        const QString y4 = coordinateparser::format_number(y + 0.8);
        cmd = "  \\draw (" + x0 + "," + y0 + ") .. controls (" + x2 + "," + y2 + ") and (" + x3 + "," + y3 +
              ") .. (" + x4 + "," + y4 + ");\n";
    } else if (add_object_mode_ == "node") {
        cmd = "  \\node at (" + x0 + "," + y0 + ") {Text};\n";
    } else {
        return;
    }
    text.insert(pos, cmd);
    replace_editor_text_preserve_undo(text);
    set_add_object_mode(QString());
    request_compile(true);
}

void mainwindow::create_menu_and_toolbar() {
    auto *file_menu = menuBar()->addMenu("File");
    auto *edit_menu = menuBar()->addMenu("Edit");
    auto *view_menu = menuBar()->addMenu("View");
    auto *build_menu = menuBar()->addMenu("Build");
    auto *examples_menu = menuBar()->addMenu("Examples");
    auto *help_menu = menuBar()->addMenu("Help");

    auto *new_act = new QAction(QIcon::fromTheme("document-new"), "New", this);
    auto *load_act = new QAction(QIcon::fromTheme("document-open"), "Load", this);
    auto *save_act = new QAction(QIcon::fromTheme("document-save"), "Save", this);
    auto *save_as_act = new QAction(QIcon::fromTheme("document-save-as"), "Save As...", this);
    auto *undo_act = new QAction(QIcon::fromTheme("edit-undo"), "Undo", this);
    auto *redo_act = new QAction(QIcon::fromTheme("edit-redo"), "Redo", this);
    auto *indent_act = new QAction(QIcon::fromTheme("format-indent-more"), "Indent", this);
    auto *left_panel_act = new QAction(QIcon::fromTheme("view-sidebar"), "Objects Panel", this);
    auto *settings_act = new QAction(QIcon::fromTheme("preferences-system"), "Settings...", this);
    auto *compile_act = new QAction(QIcon::fromTheme("system-run"), "Compile", this);
    auto *quit_act = new QAction(QIcon::fromTheme("application-exit"), "Quit", this);
    const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
    auto *about_ktikz_act = new QAction(QIcon::fromTheme("help-about"), "About " + app_name, this);

    load_act->setIconVisibleInMenu(true);
    compile_act->setIconVisibleInMenu(true);
    quit_act->setIconVisibleInMenu(true);

    new_act->setShortcut(QKeySequence::New);
    load_act->setShortcut(QKeySequence("Ctrl+O"));
    save_act->setShortcut(QKeySequence::Save);
    save_as_act->setShortcut(QKeySequence("Ctrl+Shift+S"));
    undo_act->setShortcut(QKeySequence::Undo);
    redo_act->setShortcut(QKeySequence::Redo);
    indent_act->setShortcut(QKeySequence("Ctrl+Shift+I"));
    left_panel_act->setShortcut(QKeySequence("F9"));
    settings_act->setShortcut(QKeySequence("Ctrl+,"));
    compile_act->setShortcut(QKeySequence("F5"));
    quit_act->setShortcut(QKeySequence::Quit);

    connect(new_act, &QAction::triggered, this, &mainwindow::new_file);
    connect(load_act, &QAction::triggered, this, &mainwindow::load_file);
    connect(save_act, &QAction::triggered, this, &mainwindow::save_file);
    connect(save_as_act, &QAction::triggered, this, &mainwindow::save_file_as);
    connect(undo_act, &QAction::triggered, this, [this]() {
        if (editor_) {
            editor_->undo();
        }
    });
    connect(redo_act, &QAction::triggered, this, [this]() {
        if (editor_) {
            editor_->redo();
        }
    });
    connect(indent_act, &QAction::triggered, this, &mainwindow::indent_latex);
    connect(left_panel_act, &QAction::triggered, this, &mainwindow::toggle_left_panel);
    connect(settings_act, &QAction::triggered, this, &mainwindow::open_settings);
    connect(compile_act, &QAction::triggered, this, &mainwindow::compile);
    connect(quit_act, &QAction::triggered, this, &QWidget::close);
    connect(about_ktikz_act, &QAction::triggered, this, [this]() {
        const QString app_name = QString::fromLatin1(appconfig::APP_NAME);
        QMessageBox::about(
            this,
            "About " + app_name,
            app_name + "\n\n"
            "A Qt editor for TikZ with live PDF preview and interactive shape editing.\n\n"
            "Francesco Betti Sorbelli <francesco.bettisorbelli@unipg.it>");
    });

    auto add_example = [this, examples_menu](const QString &label, const QString &body) {
        auto *act = new QAction(label, this);
        connect(act, &QAction::triggered, this, [this, body, label]() {
            if (!editor_) {
                return;
            }
            replace_editor_text_preserve_undo(wrap_tikz_document(body));
            statusBar()->showMessage("Loaded example: " + label, 1500);
            if (compile_service_ && !compile_service_->is_busy()) {
                compile();
            }
        });
        examples_menu->addAction(act);
    };

    add_example("Line", "  \\draw[thick] (0,0) -- (4,2);\n");
    add_example("Polyline", "  \\draw[blue,dashed,thick,->] (0,0) -- (1,1.5) -- (2.2,0.3) -- (3.8,1.8);\n");
    add_example("Circle", "  \\draw[thick] (0,0) circle (1.5);\n");
    add_example("Rectangle", "  \\draw[thick] (-1.5,-1) rectangle (2,1.2);\n");
    add_example("Ellipse", "  \\draw[thick] (0,0) ellipse (2 and 1);\n");
    add_example(
        "Bezier",
        "  \\draw[blue,very thick] (0,0) .. controls (1.5,2.0) and (3.0,-1.0) .. (4.0,1.0)\n"
        "                       .. controls (5.2,2.3) and (6.8,0.2) .. (8.0,1.6);\n");
    add_example(
        "Mixed Playground",
        "  \\draw[->,thick] (-9,0) -- (9,0);\n"
        "  \\draw[->,thick] (0,-9) -- (0,9);\n"
        "  \\draw[blue,dashed,ultra thick,->] (1,1) -- (2,3) -- (4,1) -- (6,3)"
        "                                -- (7.2,2.2) -- (8.0,4.0) -- (8.8,3.1);\n"
        "  \\draw[brown, thick, fill=yellow, fill opacity=0.6] (-4,-3) circle (2);\n"
        "  \\draw[magenta, ultra thick, fill=green!20] (6,5) ellipse (2 and 1);\n"
        "  \\draw[orange,thick,fill=red,fill opacity=0.4] (2,-4) rectangle (6,-1);\n"
        "  \\draw[red,thick] (-4,4) .. controls (-6,6) and (-1,4) .. (-3,7)"
        "                   .. controls (-2,8.2) and (0.5,6.3) .. (1.2,7.1);\n"
        "  \\node at (2,6) {" + app_name + " ... enjoy};\n");

    file_menu->addAction(new_act);
    file_menu->addAction(load_act);
    file_menu->addAction(save_act);
    file_menu->addAction(save_as_act);
    file_menu->addSeparator();
    file_menu->addAction(quit_act);
    edit_menu->addAction(undo_act);
    edit_menu->addAction(redo_act);
    edit_menu->addSeparator();
    edit_menu->addAction(settings_act);
    view_menu->addAction(left_panel_act);
    build_menu->addAction(compile_act);
    build_menu->addAction(indent_act);
    help_menu->addAction(about_ktikz_act);

    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(new_act);
    toolbar->addAction(load_act);
    toolbar->addSeparator();
    toolbar->addAction(save_act);
    toolbar->addAction(save_as_act);
    toolbar->addSeparator();
    toolbar->addAction(undo_act);
    toolbar->addAction(redo_act);
    toolbar->addSeparator();
    toolbar->addAction(left_panel_act);
    toolbar->addSeparator();
    toolbar->addAction(indent_act);
    toolbar->addAction(compile_act);
}

void mainwindow::apply_editor_font_size(int size) {
    editor_font_size_ = qBound(8, size, 32);
    if (!editor_) {
        return;
    }
    QFont font = editor_->font();
    font.setPointSize(editor_font_size_);
    editor_->setFont(font);
}

void mainwindow::apply_editor_font_family(const QString &family) {
    const QString normalized = family.trimmed();
    if (!normalized.isEmpty()) {
        editor_font_family_ = normalized;
    }
    if (!editor_) {
        return;
    }
    QFont font = editor_->font();
    font.setFamily(editor_font_family_);
    font.setStyleHint(QFont::Monospace);
    editor_->setFont(font);
}

void mainwindow::apply_line_number_visibility(bool visible) {
    show_line_numbers_ = visible;
    if (!editor_) {
        return;
    }
    auto *line_editor = static_cast<linenumberedit *>(editor_);
    line_editor->set_line_numbers_visible(show_line_numbers_);
}

void mainwindow::apply_theme(const QString &theme_id) {
    const QString normalized = theme_id.trimmed().isEmpty() ? QStringLiteral("system") : theme_id.trimmed();
    theme_id_ = normalized;

    if (!qApp) {
        return;
    }
    static const QString startup_style_name = qApp->style() ? qApp->style()->objectName() : QString();
    static const QPalette startup_palette = qApp->palette();

    if (theme_id_ == "dark") {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        qApp->setPalette(make_dark_palette());
        qApp->setStyleSheet(
            "QMenuBar{background-color:#2d2d2d;color:#e6e6e6;}"
            "QMenuBar::item:selected{background-color:#3a3a3a;}"
            "QMenu{background-color:#2d2d2d;color:#e6e6e6;border:1px solid #3f3f3f;}"
            "QMenu::item:selected{background-color:#3d6ea8;}"
            "QToolBar{background-color:#2d2d2d;border-bottom:1px solid #3f3f3f;}"
            "QStatusBar{background-color:#2d2d2d;color:#e6e6e6;}"
            "QLabel{color:#e6e6e6;}"
            "QFrame#leftToolStripe{background-color:#1f2937;border-right:1px solid #374151;}"
            "QWidget#previewRightPane{background-color:#2d2d2d;}"
            "QWidget#previewControlsRow{background-color:#2d2d2d;}"
            "QWidget#propertiesPane{background-color:#2d2d2d;}"
            "QLabel#selectionPill{background-color:#1f2937;color:#e5e7eb;border:1px solid #4b5563;border-radius:8px;padding:4px 8px;}");
        return;
    }

    if (theme_id_ == "light") {
        if (!startup_style_name.isEmpty()) {
            qApp->setStyle(QStyleFactory::create(startup_style_name));
        }
        qApp->setPalette(startup_palette);
        qApp->setStyleSheet(QString());
        return;
    }

    if (!startup_style_name.isEmpty()) {
        qApp->setStyle(QStyleFactory::create(startup_style_name));
    }
    qApp->setPalette(startup_palette);
    qApp->setStyleSheet(QString());
}

void mainwindow::load_settings() {
    QSettings settings;
    const QString saved_font_family = settings.value("ui/editor_font_family", editor_font_family_).toString();
    const int saved_font_size = settings.value("ui/editor_font_size", editor_font_size_).toInt();
    const bool saved_line_numbers = settings.value("ui/show_line_numbers", show_line_numbers_).toBool();
    const QString saved_theme = settings.value("ui/theme", theme_id_).toString();
    const int saved_delay_ms = settings.value("build/auto_compile_delay_ms", auto_compile_delay_ms_).toInt();
    const QString saved_compiler = settings.value("build/compiler_command", compiler_command_).toString();
    const QString saved_file_directory = settings.value("file/last_directory").toString();
    const int saved_step_mm = settings.value("grid/step_mm", grid_snap_mm_).toInt();
    const int saved_extent_cm = settings.value("grid/extent_cm", grid_extent_cm_).toInt();

    apply_editor_font_family(saved_font_family);
    apply_editor_font_size(saved_font_size);
    apply_line_number_visibility(saved_line_numbers);
    apply_theme(saved_theme);
    auto_compile_delay_ms_ = qBound(100, saved_delay_ms, 3000);
    if (auto_compile_timer_) {
        auto_compile_timer_->setInterval(auto_compile_delay_ms_);
    }
    compiler_command_ = saved_compiler.trimmed().isEmpty() ? QStringLiteral("pdflatex") : saved_compiler.trimmed();
    last_file_directory_ = saved_file_directory;
    if (compile_service_) {
        compile_service_->set_compiler_command(compiler_command_);
    }

    grid_extent_cm_ = qBound(20, saved_extent_cm, 100);
    if (grid_extent_spin_) {
        const QSignalBlocker blocker(grid_extent_spin_);
        grid_extent_spin_->setValue(grid_extent_cm_);
    }

    int normalized_step = saved_step_mm;
    if (normalized_step != 10 && normalized_step != 5 && normalized_step != 2 && normalized_step != 1 &&
        normalized_step != 0) {
        normalized_step = 10;
    }
    grid_snap_mm_ = normalized_step;
    grid_display_mm_ = (grid_snap_mm_ == 0) ? 10 : grid_snap_mm_;
    preview_canvas_->set_snap_mm(grid_snap_mm_);
    if (grid_step_combo_) {
        const QSignalBlocker blocker(grid_step_combo_);
        const int idx = grid_step_combo_->findData(grid_snap_mm_);
        grid_step_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
}

void mainwindow::save_settings() const {
    QSettings settings;
    settings.setValue("ui/editor_font_family", editor_font_family_);
    settings.setValue("ui/editor_font_size", editor_font_size_);
    settings.setValue("ui/show_line_numbers", show_line_numbers_);
    settings.setValue("ui/theme", theme_id_);
    settings.setValue("build/auto_compile_delay_ms", auto_compile_delay_ms_);
    settings.setValue("build/compiler_command", compiler_command_);
    settings.setValue("file/last_directory", last_file_directory_);
    settings.setValue("grid/step_mm", grid_snap_mm_);
    settings.setValue("grid/extent_cm", grid_extent_cm_);
}

void mainwindow::open_settings() {
    settingsdialog dialog(this);
    dialog.set_editor_font_family(editor_font_family_);
    dialog.set_editor_font_size(editor_font_size_);
    dialog.set_show_line_numbers(show_line_numbers_);
    dialog.set_theme(theme_id_);
    dialog.set_auto_compile_delay_ms(auto_compile_delay_ms_);
    dialog.set_compiler_command(compiler_command_);
    dialog.set_grid_step_mm(grid_snap_mm_);
    dialog.set_grid_extent_cm(grid_extent_cm_);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    apply_editor_font_family(dialog.editor_font_family());
    apply_editor_font_size(dialog.editor_font_size());
    apply_line_number_visibility(dialog.show_line_numbers());
    apply_theme(dialog.theme());
    auto_compile_delay_ms_ = qBound(100, dialog.auto_compile_delay_ms(), 3000);
    if (auto_compile_timer_) {
        auto_compile_timer_->setInterval(auto_compile_delay_ms_);
    }
    compiler_command_ = dialog.compiler_command().trimmed().isEmpty() ? QStringLiteral("pdflatex")
                                                                      : dialog.compiler_command().trimmed();
    if (compile_service_) {
        compile_service_->set_compiler_command(compiler_command_);
    }

    grid_extent_cm_ = qBound(20, dialog.grid_extent_cm(), 100);
    if (grid_extent_spin_) {
        const QSignalBlocker blocker(grid_extent_spin_);
        grid_extent_spin_->setValue(grid_extent_cm_);
    }

    const int new_step = dialog.grid_step_mm();
    grid_snap_mm_ = qMax(0, new_step);
    grid_display_mm_ = (grid_snap_mm_ == 0) ? 10 : grid_snap_mm_;
    preview_canvas_->set_snap_mm(grid_snap_mm_);
    if (grid_step_combo_) {
        const QSignalBlocker blocker(grid_step_combo_);
        const int idx = grid_step_combo_->findData(grid_snap_mm_);
        grid_step_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    save_settings();
    request_compile(true);
    statusBar()->showMessage("Settings updated", 2000);
}

void mainwindow::new_file() {
    if (!editor_) {
        return;
    }

    if (!maybe_save_before_action("New file", "Save changes before creating a new file?")) {
        return;
    }

    replace_editor_text_preserve_undo(QString());
    current_file_path_.clear();
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("New file", 1500);
}

void mainwindow::load_file() {
    if (!maybe_save_before_action("Open file", "Save changes before opening another file?")) {
        return;
    }

    QString initial_directory = last_file_directory_;
    if (!current_file_path_.isEmpty()) {
        initial_directory = QFileInfo(current_file_path_).absolutePath();
    }
    if (initial_directory.isEmpty() || !QDir(initial_directory).exists()) {
        initial_directory = QDir::homePath();
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        "Load TikZ/LaTeX File",
        initial_directory,
        "TeX files (*.tex *.tikz);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage("Load failed", 3000);
        return;
    }

    QTextStream in(&file);
    replace_editor_text_preserve_undo(in.readAll());
    editor_->document()->setModified(false);
    current_file_path_ = path;
    last_file_directory_ = QFileInfo(path).absolutePath();
    save_settings();
    update_window_title();
    statusBar()->showMessage("Loaded " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::save_file() {
    if (!editor_) {
        return;
    }

    QString path = current_file_path_;
    if (path.isEmpty()) {
        save_file_as();
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        statusBar()->showMessage("Save failed", 3000);
        return;
    }

    QTextStream out(&file);
    out << editor_->toPlainText();
    file.close();

    current_file_path_ = path;
    last_file_directory_ = QFileInfo(path).absolutePath();
    save_settings();
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::save_file_as() {
    if (!editor_) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save As",
        !current_file_path_.isEmpty()
            ? current_file_path_
            : (last_file_directory_.isEmpty() ? QDir::homePath() : last_file_directory_),
        "TeX files (*.tex *.tikz);;All files (*)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        statusBar()->showMessage("Save failed", 3000);
        return;
    }

    QTextStream out(&file);
    out << editor_->toPlainText();
    file.close();

    current_file_path_ = path;
    last_file_directory_ = QFileInfo(path).absolutePath();
    save_settings();
    editor_->document()->setModified(false);
    update_window_title();
    statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 3000);
}

void mainwindow::on_document_modified_changed(bool) {
    update_window_title();
}

void mainwindow::compile() {
    request_compile(true);
}

void mainwindow::indent_latex() {
    if (!editor_) {
        return;
    }

    const QString text = editor_->toPlainText();
    const QStringList lines = text.split('\n', Qt::KeepEmptyParts);

    auto count_occurrences = [](const QString &line, const QString &token) {
        int count = 0;
        int pos = 0;
        while ((pos = line.indexOf(token, pos)) >= 0) {
            ++count;
            pos += token.size();
        }
        return count;
    };

    QStringList out_lines;
    out_lines.reserve(lines.size());
    int indent_level = 0;

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();

        int pre_dedent = 0;
        if (trimmed.startsWith("\\end{")) {
            pre_dedent = 1;
        }
        indent_level = qMax(0, indent_level - pre_dedent);

        if (trimmed.isEmpty()) {
            out_lines.push_back(QString());
        } else {
            out_lines.push_back(QString(indent_level * 2, ' ') + trimmed);
        }

        const int begins = count_occurrences(trimmed, "\\begin{");
        const int ends = count_occurrences(trimmed, "\\end{");
        indent_level = qMax(0, indent_level + begins - ends);
    }

    editor_->setPlainText(out_lines.join('\n'));
    statusBar()->showMessage("LaTeX indentation applied", 1500);
}

void mainwindow::on_compile_service_output(const QString &text) {
    const QString lower = text.toLower();
    const bool has_error = lower.contains("error") || lower.contains("failed");
    const QColor color = has_error
                             ? (theme_id_ == "dark" ? QColor("#f87171") : QColor("#b91c1c"))
                             : palette().color(QPalette::Text);
    append_colored_log(output_, text, color);
}

void mainwindow::on_compile_finished(bool success, const QString &pdf_path, const QString &message) {
    if (message != "canceled") {
        if (!success) {
            append_colored_log(
                output_, "[Status] Compiled with errors", theme_id_ == "dark" ? QColor("#f87171") : QColor("#dc2626"));
            statusBar()->showMessage("Compile failed", 3000);
        } else if (!preview_canvas_->load_pdf(pdf_path)) {
            on_compile_service_output("[Preview] Failed to load generated PDF");
            append_colored_log(
                output_, "[Status] Compiled with errors", theme_id_ == "dark" ? QColor("#f87171") : QColor("#dc2626"));
            statusBar()->showMessage("Preview load failed", 3000);
        } else {
            append_colored_log(
                output_, "[Status] Compiled successfully", theme_id_ == "dark" ? QColor("#86efac") : QColor("#16a34a"));
            statusBar()->showMessage("Compile successful", 2500);
            if (!preview_canvas_->has_calibration()) {
                on_compile_service_output("[Preview] Coordinate calibration unavailable: visual editing disabled.");
                statusBar()->showMessage("Preview available; coordinate calibration unavailable", 5000);
            }
        }
    }

    if (pending_compile_) {
        pending_compile_ = false;
        request_compile(false);
    }
}
