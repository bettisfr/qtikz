#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <tuple>
#include <vector>

#include "model.h"

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QSpinBox;
class QTextEdit;
class QTimer;
class QToolButton;
class QWidget;

class compileservice;
class pdfcanvas;

class mainwindow : public QMainWindow {
    Q_OBJECT

public:
    explicit mainwindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void new_file();
    void load_file();
    void save_file();
    void save_file_as();
    void compile();
    void indent_latex();
    void on_compile_service_output(const QString &text);
    void on_compile_finished(bool success, const QString &pdf_path, const QString &message);
    void on_coordinate_dragged(int index, double x, double y);
    void on_circle_radius_dragged(int index, double radius);
    void on_ellipse_radii_dragged(int index, double rx, double ry);
    void on_bezier_control_dragged(int index, int control_idx, double x, double y);
    void on_rectangle_corner_dragged(int index, double x2, double y2);
    void on_grid_step_changed(int value);
    void on_grid_extent_changed(int value);
    void on_canvas_selection_changed(const QString &type, int index, int subindex);
    void apply_selected_geometry_changes();
    void apply_selected_style_changes();
    void delete_selected_object();
    void start_add_line_mode();
    void start_add_polyline_mode();
    void start_add_circle_mode();
    void start_add_rectangle_mode();
    void start_add_ellipse_mode();
    void start_add_bezier_mode();
    void start_add_node_mode();
    void on_canvas_add_point(double x, double y);
    void on_document_modified_changed(bool modified);
    void on_editor_text_changed();
    void on_auto_compile_timeout();
    void open_settings();
    void toggle_left_panel();

private:
    void create_menu_and_toolbar();
    void update_window_title();
    bool maybe_save_before_action(const QString &title, const QString &text);
    void request_compile(bool cancel_running);
    void replace_editor_text_preserve_undo(const QString &text);
    void apply_editor_font_size(int size);
    void apply_editor_font_family(const QString &family);
    void apply_line_number_visibility(bool visible);
    void apply_theme(const QString &theme_id);
    void load_settings();
    void save_settings() const;
    void update_properties_panel();
    void clear_properties_panel(const QString &message = QStringLiteral("No object selected"));
    static QString minimal_tikz_document();
    void ensure_minimal_document_loaded();
    void set_add_object_mode(const QString &mode);
    bool replace_segments(QString &text, const std::vector<std::tuple<int, int, QString>> &segments);
    int selected_anchor_position() const;
    bool selected_command_span(int &start_out, int &end_out) const;

    QPlainTextEdit *editor_ = nullptr;
    QWidget *left_panel_ = nullptr;
    QSplitter *left_main_splitter_ = nullptr;
    QToolButton *left_panel_toggle_button_ = nullptr;
    QPushButton *left_line_button_ = nullptr;
    QPushButton *left_polyline_button_ = nullptr;
    QPushButton *left_circle_button_ = nullptr;
    QPushButton *left_rectangle_button_ = nullptr;
    QPushButton *left_ellipse_button_ = nullptr;
    QPushButton *left_bezier_button_ = nullptr;
    QPushButton *left_node_button_ = nullptr;
    pdfcanvas *preview_canvas_ = nullptr;
    QTextEdit *output_ = nullptr;
    QComboBox *grid_step_combo_ = nullptr;
    QSpinBox *grid_extent_spin_ = nullptr;
    QLabel *props_selection_value_ = nullptr;
    QDoubleSpinBox *props_value_1_ = nullptr;
    QDoubleSpinBox *props_value_2_ = nullptr;
    QDoubleSpinBox *props_value_3_ = nullptr;
    QDoubleSpinBox *props_value_4_ = nullptr;
    QLabel *props_label_1_ = nullptr;
    QLabel *props_label_2_ = nullptr;
    QLabel *props_label_3_ = nullptr;
    QLabel *props_label_4_ = nullptr;
    QComboBox *props_color_combo_ = nullptr;
    QComboBox *props_endpoint_start_combo_ = nullptr;
    QComboBox *props_endpoint_end_combo_ = nullptr;
    QComboBox *props_line_style_combo_ = nullptr;
    QComboBox *props_thickness_combo_ = nullptr;
    QComboBox *props_draw_opacity_combo_ = nullptr;
    QComboBox *props_fill_color_combo_ = nullptr;
    QComboBox *props_fill_opacity_combo_ = nullptr;
    QPushButton *props_delete_btn_ = nullptr;
    compileservice *compile_service_ = nullptr;
    QTimer *auto_compile_timer_ = nullptr;

    std::vector<coord_ref> coordinate_refs_;
    std::vector<circle_ref> circle_refs_;
    std::vector<ellipse_ref> ellipse_refs_;
    std::vector<bezier_ref> bezier_refs_;
    std::vector<rectangle_ref> rectangle_refs_;
    int grid_snap_mm_ = 10;
    int grid_display_mm_ = 10;
    int grid_extent_cm_ = 20;
    QString editor_font_family_ = QStringLiteral("Monospace");
    int editor_font_size_ = 12;
    bool show_line_numbers_ = true;
    int auto_compile_delay_ms_ = 450;
    QString compiler_command_ = QStringLiteral("pdflatex");
    QString last_file_directory_;
    QString theme_id_ = QStringLiteral("system");
    bool suppress_auto_compile_ = false;
    bool pending_compile_ = false;
    bool suppress_properties_apply_ = false;
    QString add_object_mode_;
    QString selected_type_;
    int selected_index_ = -1;
    int selected_subindex_ = -1;
    QString current_file_path_;
};

#endif
