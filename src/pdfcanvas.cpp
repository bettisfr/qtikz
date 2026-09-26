#include "pdfcanvas.h"

#include <QLineF>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <cmath>

pdfcanvas::pdfcanvas(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

void pdfcanvas::set_coordinates(const std::vector<coord_pair> &coords) {
    coordinates_ = coords;
    update();
}

void pdfcanvas::set_circles(const std::vector<circle_pair> &circles) {
    circles_ = circles;
    update();
}

void pdfcanvas::set_ellipses(const std::vector<ellipse_pair> &ellipses) {
    ellipses_ = ellipses;
    update();
}

void pdfcanvas::set_beziers(const std::vector<bezier_pair> &beziers) {
    beziers_ = beziers;
    update();
}

void pdfcanvas::set_rectangles(const std::vector<rectangle_pair> &rectangles) {
    rectangles_ = rectangles;
    update();
}

void pdfcanvas::set_snap_mm(int mm) {
    snap_mm_ = qMax(0, mm);
}

void pdfcanvas::set_add_line_mode(bool enabled) {
    add_line_mode_ = enabled;
}

bool pdfcanvas::load_pdf(const QString &pdf_path) {
    calibration_valid_ = false;
    page_calibration_valid_ = false;
    rendered_image_ = QImage();
    rendered_size_ = QSize();
    pdf_document_.close();
    const QPdfDocument::Error err = pdf_document_.load(pdf_path);
    if (err == QPdfDocument::Error::None && pdf_document_.pageCount() > 0) {
        page_calibration_valid_ = load_calibration(pdf_path);
        if (!page_calibration_valid_) {
            qWarning("Preview calibration unavailable; coordinate editing disabled.");
        }
    }
    update();
    return err == QPdfDocument::Error::None;
}

void pdfcanvas::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));

    if (pdf_document_.status() != QPdfDocument::Status::Ready || pdf_document_.pageCount() <= 0) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, "Compile to preview output");
        return;
    }

    const QSizeF page_size = pdf_document_.pagePointSize(0);
    if (page_size.width() <= 0 || page_size.height() <= 0) {
        return;
    }

    const double fit = 0.95 * qMin(width() / page_size.width(), height() / page_size.height());
    const double scale = fit * view_scale_;
    const int w = qMax(1, static_cast<int>(page_size.width() * scale));
    const int h = qMax(1, static_cast<int>(page_size.height() * scale));
    const QRect target_rect(static_cast<int>((width() - w) * 0.5 + pan_offset_.x()),
                            static_cast<int>((height() - h) * 0.5 + pan_offset_.y()),
                            w,
                            h);
    painter.fillRect(target_rect, QColor("#ffffff"));

    if (rendered_image_.isNull() || rendered_size_ != target_rect.size()) {
        rendered_image_ = pdf_document_.render(0, target_rect.size());
        rendered_size_ = target_rect.size();
    }

    if (!rendered_image_.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target_rect, rendered_image_);
    }

    update_calibration(target_rect);
    draw_coordinate_markers(painter);
    draw_circle_markers(painter);
    draw_ellipse_markers(painter);
    draw_bezier_markers(painter);
    draw_rectangle_markers(painter);
}

void pdfcanvas::wheelEvent(QWheelEvent *event) {
    const QPoint delta = event->angleDelta();
    if (delta.y() == 0) {
        event->ignore();
        return;
    }

    const double steps = static_cast<double>(delta.y()) / 120.0;
    view_scale_ *= std::pow(1.12, steps);
    view_scale_ = qBound(0.2, view_scale_, 12.0);
    rendered_image_ = QImage();
    rendered_size_ = QSize();
    update();
    event->accept();
}

void pdfcanvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (add_line_mode_) {
            QPointF world;
            if (screen_to_world(event->position(), world)) {
                if (snap_mm_ > 0) {
                    const double step = static_cast<double>(snap_mm_) / 10.0;
                    world.setX(std::round(world.x() / step) * step);
                    world.setY(std::round(world.y() / step) * step);
                }
                emit add_point_clicked(world.x(), world.y());
                event->accept();
                return;
            }
        }
        if (calibration_valid_) {
            const int rect_idx = hit_test_rectangle_marker(event->position());
            if (rect_idx >= 0) {
                emit selection_changed("rectangle", rect_idx, -1);
                rectangle_dragging_ = true;
                active_rectangle_index_ = rect_idx;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            const int circle_idx = hit_test_circle_marker(event->position());
            if (circle_idx >= 0) {
                emit selection_changed("circle", circle_idx, -1);
                circle_dragging_ = true;
                active_circle_index_ = circle_idx;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            const int ellipse_rx_idx = hit_test_ellipse_rx_marker(event->position());
            if (ellipse_rx_idx >= 0) {
                emit selection_changed("ellipse", ellipse_rx_idx, 0);
                ellipse_dragging_ = true;
                active_ellipse_index_ = ellipse_rx_idx;
                ellipse_dragging_rx_ = true;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            const int ellipse_ry_idx = hit_test_ellipse_ry_marker(event->position());
            if (ellipse_ry_idx >= 0) {
                emit selection_changed("ellipse", ellipse_ry_idx, 1);
                ellipse_dragging_ = true;
                active_ellipse_index_ = ellipse_ry_idx;
                ellipse_dragging_rx_ = false;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            const int bezier_c1_idx = hit_test_bezier_c1_marker(event->position());
            if (bezier_c1_idx >= 0) {
                emit selection_changed("bezier", bezier_c1_idx, 1);
                bezier_dragging_ = true;
                active_bezier_index_ = bezier_c1_idx;
                bezier_dragging_c1_ = true;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            const int bezier_c2_idx = hit_test_bezier_c2_marker(event->position());
            if (bezier_c2_idx >= 0) {
                emit selection_changed("bezier", bezier_c2_idx, 2);
                bezier_dragging_ = true;
                active_bezier_index_ = bezier_c2_idx;
                bezier_dragging_c1_ = false;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
            const int idx = hit_test_marker(event->position());
            if (idx >= 0) {
                emit selection_changed("coordinate", idx, -1);
                marker_dragging_ = true;
                active_marker_index_ = idx;
                setCursor(Qt::CrossCursor);
                event->accept();
                return;
            }
        }
        emit selection_changed(QString(), -1, -1);
        dragging_ = true;
        last_drag_pos_ = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void pdfcanvas::mouseMoveEvent(QMouseEvent *event) {
    if (rectangle_dragging_ && active_rectangle_index_ >= 0 &&
        active_rectangle_index_ < static_cast<int>(rectangles_.size())) {
        QPointF world;
        if (screen_to_world(event->position(), world)) {
            if (snap_mm_ > 0) {
                const double step = static_cast<double>(snap_mm_) / 10.0;
                world.setX(std::round(world.x() / step) * step);
                world.setY(std::round(world.y() / step) * step);
            }
            rectangles_[active_rectangle_index_].x2 = world.x();
            rectangles_[active_rectangle_index_].y2 = world.y();
            update();
        }
        event->accept();
        return;
    }

    if (circle_dragging_ && active_circle_index_ >= 0 && active_circle_index_ < static_cast<int>(circles_.size())) {
        QPointF world;
        if (screen_to_world(event->position(), world)) {
            const circle_pair c = circles_[active_circle_index_];
            double r = std::hypot(world.x() - c.cx, world.y() - c.cy);
            if (snap_mm_ > 0) {
                const double step = static_cast<double>(snap_mm_) / 10.0;
                r = std::round(r / step) * step;
            }
            circles_[active_circle_index_].r = qMax(0.01, r);
            update();
        }
        event->accept();
        return;
    }

    if (ellipse_dragging_ && active_ellipse_index_ >= 0 && active_ellipse_index_ < static_cast<int>(ellipses_.size())) {
        QPointF world;
        if (screen_to_world(event->position(), world)) {
            ellipse_pair e = ellipses_[active_ellipse_index_];
            if (ellipse_dragging_rx_) {
                double rx = std::abs(world.x() - e.cx);
                if (snap_mm_ > 0) {
                    const double step = static_cast<double>(snap_mm_) / 10.0;
                    rx = std::round(rx / step) * step;
                }
                e.rx = qMax(0.01, rx);
            } else {
                double ry = std::abs(world.y() - e.cy);
                if (snap_mm_ > 0) {
                    const double step = static_cast<double>(snap_mm_) / 10.0;
                    ry = std::round(ry / step) * step;
                }
                e.ry = qMax(0.01, ry);
            }
            ellipses_[active_ellipse_index_] = e;
            update();
        }
        event->accept();
        return;
    }

    if (bezier_dragging_ && active_bezier_index_ >= 0 && active_bezier_index_ < static_cast<int>(beziers_.size())) {
        QPointF world;
        if (screen_to_world(event->position(), world)) {
            if (snap_mm_ > 0) {
                const double step = static_cast<double>(snap_mm_) / 10.0;
                world.setX(std::round(world.x() / step) * step);
                world.setY(std::round(world.y() / step) * step);
            }

            bezier_pair b = beziers_[active_bezier_index_];
            if (bezier_dragging_c1_) {
                b.x1 = world.x();
                b.y1 = world.y();
            } else {
                b.x2 = world.x();
                b.y2 = world.y();
            }
            beziers_[active_bezier_index_] = b;
            update();
        }
        event->accept();
        return;
    }

    if (marker_dragging_ && active_marker_index_ >= 0 && active_marker_index_ < static_cast<int>(coordinates_.size())) {
        QPointF world;
        if (screen_to_world(event->position(), world)) {
            if (snap_mm_ > 0) {
                const double step = static_cast<double>(snap_mm_) / 10.0;
                world.setX(std::round(world.x() / step) * step);
                world.setY(std::round(world.y() / step) * step);
            }
            coordinates_[active_marker_index_].x = world.x();
            coordinates_[active_marker_index_].y = world.y();
            update();
        }
        event->accept();
        return;
    }

    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF delta = event->position() - last_drag_pos_;
    pan_offset_ += delta;
    last_drag_pos_ = event->position();
    update();
    event->accept();
}

void pdfcanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && rectangle_dragging_) {
        rectangle_dragging_ = false;
        unsetCursor();
        if (active_rectangle_index_ >= 0 && active_rectangle_index_ < static_cast<int>(rectangles_.size())) {
            const rectangle_pair &r = rectangles_[active_rectangle_index_];
            emit rectangle_corner_dragged(active_rectangle_index_, r.x2, r.y2);
        }
        active_rectangle_index_ = -1;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && circle_dragging_) {
        circle_dragging_ = false;
        unsetCursor();
        if (active_circle_index_ >= 0 && active_circle_index_ < static_cast<int>(circles_.size())) {
            const circle_pair &c = circles_[active_circle_index_];
            emit circle_radius_dragged(active_circle_index_, c.r);
        }
        active_circle_index_ = -1;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && ellipse_dragging_) {
        ellipse_dragging_ = false;
        unsetCursor();
        if (active_ellipse_index_ >= 0 && active_ellipse_index_ < static_cast<int>(ellipses_.size())) {
            const ellipse_pair &e = ellipses_[active_ellipse_index_];
            emit ellipse_radii_dragged(active_ellipse_index_, e.rx, e.ry);
        }
        active_ellipse_index_ = -1;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && bezier_dragging_) {
        bezier_dragging_ = false;
        unsetCursor();
        if (active_bezier_index_ >= 0 && active_bezier_index_ < static_cast<int>(beziers_.size())) {
            const bezier_pair &b = beziers_[active_bezier_index_];
            if (bezier_dragging_c1_) {
                emit bezier_control_dragged(active_bezier_index_, 1, b.x1, b.y1);
            } else {
                emit bezier_control_dragged(active_bezier_index_, 2, b.x2, b.y2);
            }
        }
        active_bezier_index_ = -1;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && marker_dragging_) {
        marker_dragging_ = false;
        unsetCursor();
        if (active_marker_index_ >= 0 && active_marker_index_ < static_cast<int>(coordinates_.size())) {
            const coord_pair &c = coordinates_[active_marker_index_];
            emit coordinate_dragged(active_marker_index_, c.x, c.y);
        }
        active_marker_index_ = -1;
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        unsetCursor();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

bool pdfcanvas::load_calibration(const QString &pdf_path) {
    QFile positions(QFileInfo(pdf_path).absolutePath() + "/document.qtikz");
    QFile auxiliary(QFileInfo(pdf_path).absolutePath() + "/document.aux");
    if (!positions.open(QIODevice::ReadOnly | QIODevice::Text) ||
        !auxiliary.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&positions);
    QString version, picture_id, page_tag;
    QPointF basis[3];
    int page = 0;
    in >> version >> picture_id;
    for (QPointF &point : basis) {
        double x = 0, y = 0;
        in >> x >> y;
        point = QPointF(x, y);
    }
    in >> page_tag >> page;
    if (in.status() != QTextStream::Ok || version != "QTIKZ1" ||
        page_tag != "PAGE" || page != 1) {
        return false; // The preview displays the first physical page only.
    }
    const QRegularExpression mark(
        QStringLiteral(R"(\\pgfsyspdfmark\s*\{%1\}\{(-?\d+)\}\{(-?\d+)\})")
            .arg(QRegularExpression::escape(picture_id)));
    const auto match = mark.match(QString::fromUtf8(auxiliary.readAll()));
    if (!match.hasMatch()) {
        return false;
    }
    const QPointF origin(match.captured(1).toDouble(), match.captured(2).toDouble());
    // TeX sp -> PDF points (bp); PDF/TeX origin is bottom-left, Qt is top-left.
    constexpr double sp_to_bp = 72.0 / (72.27 * 65536.0);
    const double height = pdf_document_.pagePointSize(0).height();
    for (QPointF &point : basis) {
        point += origin;
        point = QPointF(point.x() * sp_to_bp, height - point.y() * sp_to_bp);
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            return false;
        }
    }
    const QPointF u = basis[1] - basis[0];
    const QPointF v = basis[2] - basis[0];
    if (std::abs(u.x() * v.y() - u.y() * v.x()) < 1e-9) {
        return false;
    }
    page_origin_ = basis[0];
    page_axis_x_ = basis[1];
    page_axis_y_ = basis[2];
    return true;
}

void pdfcanvas::update_calibration(const QRect &target_rect) {
    calibration_valid_ = false;
    const QSizeF page_size = pdf_document_.pagePointSize(0);
    if (!page_calibration_valid_ || !target_rect.isValid() ||
        page_size.width() <= 0 || page_size.height() <= 0) {
        return;
    }
    const auto to_screen = [&](const QPointF &point) {
        return QPointF(target_rect.left() + point.x() * target_rect.width() / page_size.width(),
                       target_rect.top() + point.y() * target_rect.height() / page_size.height());
    };
    origin_px_ = to_screen(page_origin_);
    axis_x_px_ = to_screen(page_axis_x_);
    axis_y_px_ = to_screen(page_axis_y_);
    calibration_valid_ = true;
}

QPointF pdfcanvas::world_to_screen(double x, double y) const {
    const QPointF u = axis_x_px_ - origin_px_;
    const QPointF v = axis_y_px_ - origin_px_;
    return origin_px_ + u * x + v * y;
}

bool pdfcanvas::screen_to_world(const QPointF &p, QPointF &world_out) const {
    if (!calibration_valid_) {
        return false;
    }

    const QPointF u = axis_x_px_ - origin_px_;
    const QPointF v = axis_y_px_ - origin_px_;
    const double det = u.x() * v.y() - u.y() * v.x();
    if (std::abs(det) < 1e-9) {
        return false;
    }

    const QPointF d = p - origin_px_;
    const double x = (d.x() * v.y() - d.y() * v.x()) / det;
    const double y = (u.x() * d.y() - u.y() * d.x()) / det;
    world_out = QPointF(x, y);
    return true;
}

int pdfcanvas::hit_test_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(coordinates_.size()); ++i) {
        const QPointF p = world_to_screen(coordinates_[i].x, coordinates_[i].y);
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

int pdfcanvas::hit_test_circle_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(circles_.size()); ++i) {
        const circle_pair &c = circles_[i];
        const QPointF p = world_to_screen(c.cx + c.r, c.cy); // 0 degree marker
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

int pdfcanvas::hit_test_ellipse_rx_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(ellipses_.size()); ++i) {
        const ellipse_pair &e = ellipses_[i];
        const QPointF p = world_to_screen(e.cx + e.rx, e.cy);
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

int pdfcanvas::hit_test_ellipse_ry_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(ellipses_.size()); ++i) {
        const ellipse_pair &e = ellipses_[i];
        const QPointF p = world_to_screen(e.cx, e.cy + e.ry);
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

int pdfcanvas::hit_test_bezier_c1_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(beziers_.size()); ++i) {
        const bezier_pair &b = beziers_[i];
        const QPointF p = world_to_screen(b.x1, b.y1);
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

int pdfcanvas::hit_test_bezier_c2_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(beziers_.size()); ++i) {
        const bezier_pair &b = beziers_[i];
        const QPointF p = world_to_screen(b.x2, b.y2);
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

int pdfcanvas::hit_test_rectangle_marker(const QPointF &pos) const {
    constexpr double threshold = 10.0;
    for (int i = 0; i < static_cast<int>(rectangles_.size()); ++i) {
        const rectangle_pair &r = rectangles_[i];
        const QPointF p = world_to_screen(r.x2, r.y2); // draggable corner
        if (QLineF(pos, p).length() <= threshold) {
            return i;
        }
    }
    return -1;
}

void pdfcanvas::draw_coordinate_markers(QPainter &painter) {
    if (!calibration_valid_ || coordinates_.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor("#dc2626"), 2.0));
    constexpr int half = 6;
    for (const coord_pair &c : coordinates_) {
        const QPointF p = world_to_screen(c.x, c.y);
        painter.drawLine(QPointF(p.x() - half, p.y()), QPointF(p.x() + half, p.y()));
        painter.drawLine(QPointF(p.x(), p.y() - half), QPointF(p.x(), p.y() + half));
    }
    painter.restore();
}

void pdfcanvas::draw_circle_markers(QPainter &painter) {
    if (!calibration_valid_ || circles_.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen dashed_pen(QColor(220, 38, 38, 150), 1.6, Qt::DashLine);
    dashed_pen.setCosmetic(true);
    painter.setPen(dashed_pen);
    painter.setBrush(Qt::NoBrush);
    constexpr int half = 5;
    for (const circle_pair &c : circles_) {
        const QPointF center = world_to_screen(c.cx, c.cy);
        const QPointF handle = world_to_screen(c.cx + c.r, c.cy);
        painter.drawLine(center, handle);
        painter.drawLine(QPointF(handle.x() - half, handle.y()), QPointF(handle.x() + half, handle.y()));
        painter.drawLine(QPointF(handle.x(), handle.y() - half), QPointF(handle.x(), handle.y() + half));
        painter.drawEllipse(handle, 2.0, 2.0);
    }
    painter.restore();
}

void pdfcanvas::draw_rectangle_markers(QPainter &painter) {
    if (!calibration_valid_ || rectangles_.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen dashed_pen(QColor(220, 38, 38, 150), 1.6, Qt::DashLine);
    dashed_pen.setCosmetic(true);
    painter.setPen(dashed_pen);
    constexpr int half = 5;
    for (const rectangle_pair &r : rectangles_) {
        const QPointF p1 = world_to_screen(r.x1, r.y1);
        const QPointF p2 = world_to_screen(r.x2, r.y2); // resize handle
        painter.drawLine(p1, p2);
        painter.drawLine(QPointF(p2.x() - half, p2.y()), QPointF(p2.x() + half, p2.y()));
        painter.drawLine(QPointF(p2.x(), p2.y() - half), QPointF(p2.x(), p2.y() + half));
        painter.drawEllipse(p2, 2.0, 2.0);
    }
    painter.restore();
}

void pdfcanvas::draw_ellipse_markers(QPainter &painter) {
    if (!calibration_valid_ || ellipses_.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen dashed_pen(QColor(220, 38, 38, 150), 1.6, Qt::DashLine);
    dashed_pen.setCosmetic(true);
    painter.setPen(dashed_pen);
    painter.setBrush(Qt::NoBrush);
    constexpr int half = 5;
    for (const ellipse_pair &e : ellipses_) {
        const QPointF center = world_to_screen(e.cx, e.cy);
        const QPointF hx = world_to_screen(e.cx + e.rx, e.cy);
        const QPointF hy = world_to_screen(e.cx, e.cy + e.ry);
        painter.drawLine(center, hx);
        painter.drawLine(center, hy);
        painter.drawLine(QPointF(hx.x() - half, hx.y()), QPointF(hx.x() + half, hx.y()));
        painter.drawLine(QPointF(hx.x(), hx.y() - half), QPointF(hx.x(), hx.y() + half));
        painter.drawLine(QPointF(hy.x() - half, hy.y()), QPointF(hy.x() + half, hy.y()));
        painter.drawLine(QPointF(hy.x(), hy.y() - half), QPointF(hy.x(), hy.y() + half));
        painter.drawEllipse(hx, 2.0, 2.0);
        painter.drawEllipse(hy, 2.0, 2.0);
    }
    painter.restore();
}

void pdfcanvas::draw_bezier_markers(QPainter &painter) {
    if (!calibration_valid_ || beziers_.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen dashed_pen(QColor(220, 38, 38, 150), 1.6, Qt::DashLine);
    dashed_pen.setCosmetic(true);
    painter.setPen(dashed_pen);
    painter.setBrush(Qt::NoBrush);
    constexpr int half = 5;
    for (const bezier_pair &b : beziers_) {
        const QPointF p0 = world_to_screen(b.x0, b.y0);
        const QPointF p1 = world_to_screen(b.x1, b.y1);
        const QPointF p2 = world_to_screen(b.x2, b.y2);
        const QPointF p3 = world_to_screen(b.x3, b.y3);
        painter.drawLine(p0, p1);
        painter.drawLine(p2, p3);
        painter.drawLine(QPointF(p1.x() - half, p1.y()), QPointF(p1.x() + half, p1.y()));
        painter.drawLine(QPointF(p1.x(), p1.y() - half), QPointF(p1.x(), p1.y() + half));
        painter.drawLine(QPointF(p2.x() - half, p2.y()), QPointF(p2.x() + half, p2.y()));
        painter.drawLine(QPointF(p2.x(), p2.y() - half), QPointF(p2.x(), p2.y() + half));
        painter.drawEllipse(p1, 2.0, 2.0);
        painter.drawEllipse(p2, 2.0, 2.0);
    }
    painter.restore();
}
