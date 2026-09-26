#ifndef PDFCANVAS_H
#define PDFCANVAS_H

#include <QImage>
#include <QPdfDocument>
#include <QPointF>
#include <QSize>
#include <QWidget>
#include <vector>

#include "model.h"

class pdfcanvas : public QWidget {
    Q_OBJECT

public:
    explicit pdfcanvas(QWidget *parent = nullptr);

    void set_coordinates(const std::vector<coord_pair> &coords);
    void set_circles(const std::vector<circle_pair> &circles);
    void set_ellipses(const std::vector<ellipse_pair> &ellipses);
    void set_beziers(const std::vector<bezier_pair> &beziers);
    void set_rectangles(const std::vector<rectangle_pair> &rectangles);
    void set_snap_mm(int mm);
    void set_add_line_mode(bool enabled);
    bool load_pdf(const QString &pdf_path);
    bool has_calibration() const { return page_calibration_valid_; }

signals:
    void add_point_clicked(double x, double y);
    void selection_changed(const QString &type, int index, int subindex);
    void coordinate_dragged(int index, double x, double y);
    void circle_radius_dragged(int index, double radius);
    void ellipse_radii_dragged(int index, double rx, double ry);
    void bezier_control_dragged(int index, int control_idx, double x, double y);
    void rectangle_corner_dragged(int index, double x2, double y2);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    friend class calibrationtest;
    bool load_calibration(const QString &pdf_path);

    void update_calibration(const QRect &target_rect);
    QPointF world_to_screen(double x, double y) const;
    bool screen_to_world(const QPointF &p, QPointF &world_out) const;
    int hit_test_marker(const QPointF &pos) const;
    int hit_test_circle_marker(const QPointF &pos) const;
    int hit_test_ellipse_rx_marker(const QPointF &pos) const;
    int hit_test_ellipse_ry_marker(const QPointF &pos) const;
    int hit_test_bezier_c1_marker(const QPointF &pos) const;
    int hit_test_bezier_c2_marker(const QPointF &pos) const;
    int hit_test_rectangle_marker(const QPointF &pos) const;
    void draw_coordinate_markers(QPainter &painter);
    void draw_circle_markers(QPainter &painter);
    void draw_ellipse_markers(QPainter &painter);
    void draw_bezier_markers(QPainter &painter);
    void draw_rectangle_markers(QPainter &painter);

    QPdfDocument pdf_document_;
    QImage rendered_image_;
    QSize rendered_size_;
    double view_scale_ = 1.0;
    QPointF pan_offset_{0.0, 0.0};
    bool dragging_ = false;
    QPointF last_drag_pos_{0.0, 0.0};
    bool marker_dragging_ = false;
    int active_marker_index_ = -1;
    bool circle_dragging_ = false;
    int active_circle_index_ = -1;
    bool ellipse_dragging_ = false;
    int active_ellipse_index_ = -1;
    bool ellipse_dragging_rx_ = true;
    bool bezier_dragging_ = false;
    int active_bezier_index_ = -1;
    bool bezier_dragging_c1_ = true;
    bool rectangle_dragging_ = false;
    int active_rectangle_index_ = -1;
    bool add_line_mode_ = false;
    std::vector<coord_pair> coordinates_;
    std::vector<circle_pair> circles_;
    std::vector<ellipse_pair> ellipses_;
    std::vector<bezier_pair> beziers_;
    std::vector<rectangle_pair> rectangles_;
    bool calibration_valid_ = false;
    bool page_calibration_valid_ = false;
    QPointF page_origin_;
    QPointF page_axis_x_;
    QPointF page_axis_y_;
    QPointF origin_px_{0.0, 0.0};
    QPointF axis_x_px_{1.0, 0.0};
    QPointF axis_y_px_{0.0, -1.0};
    int snap_mm_ = 10;
};

#endif
