#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>
#include <cmath>

namespace gx {

namespace {
ThemeColors g_colors;
QString g_name = "dark";
QHash<QString, QIcon> g_icons;

ThemeColors makeColors(const QString& n) {
    ThemeColors c;
    if (n == "light") {
        c.window = QColor("#e9eaed");
        c.panel = QColor("#f4f5f7");
        c.base = QColor("#ffffff");
        c.alt = QColor("#eef0f3");
        c.text = QColor("#1f2125");
        c.dim = QColor("#6c7078");
        c.button = QColor("#f8f9fa");
        c.border = QColor("#c5c8ce");
        c.accent = QColor("#2f6fdc");
        c.accentText = QColor("#ffffff");
    } else if (n == "gray") {
        c.window = QColor("#5a5b5e");
        c.panel = QColor("#636467");
        c.base = QColor("#4b4c4f");
        c.alt = QColor("#545558");
        c.text = QColor("#eceef1");
        c.dim = QColor("#b9bcc2");
        c.button = QColor("#6d6e72");
        c.border = QColor("#46474a");
        c.accent = QColor("#7fb2ff");
        c.accentText = QColor("#101418");
    } else {
        c.window = QColor("#26282c");
        c.panel = QColor("#2d3035");
        c.base = QColor("#1d1f22");
        c.alt = QColor("#25272b");
        c.text = QColor("#dfe2e7");
        c.dim = QColor("#8d929b");
        c.button = QColor("#363a40");
        c.border = QColor("#3e4249");
        c.accent = QColor("#5b9bf0");
        c.accentText = QColor("#0e1116");
    }
    c.danger = QColor("#e5534b");
    return c;
}

// Small images the stylesheet needs (check mark, arrows), drawn in the theme colors.
QString assetsDir(const ThemeColors& c) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/graphix-theme-" + c.text.name().mid(1) +
                  c.accentText.name().mid(1);
    QDir().mkpath(dir);
    auto save = [&](const QString& file, auto draw) {
        const int sz = 36;  // drawn large, scaled down sharply on any screen
        QImage img(sz, sz, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(sz / 12.0, sz / 12.0);
        draw(p);
        p.end();
        img.save(dir + "/" + file);
    };
    save("check.png", [&](QPainter& p) {
        p.setPen(QPen(c.accentText, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({QPointF(2.5, 6.3), QPointF(5, 8.8), QPointF(9.5, 3.2)}));
    });
    save("dot.png", [&](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.accentText);
        p.drawEllipse(QPointF(6, 6), 2.6, 2.6);
    });
    save("up.png", [&](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.text);
        p.drawPolygon(QPolygonF({QPointF(2, 8.5), QPointF(10, 8.5), QPointF(6, 3.5)}));
    });
    save("down.png", [&](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.text);
        p.drawPolygon(QPolygonF({QPointF(2, 3.5), QPointF(10, 3.5), QPointF(6, 8.5)}));
    });
    return dir;
}

QString css(const ThemeColors& c) {
    auto h = [](const QColor& k) { return k.name(); };
    QString s = R"(
QWidget { color: %TEXT%; }
QMainWindow, QDialog { background: %WINDOW%; }
QMainWindow::separator { background: %BORDER%; width: 3px; height: 3px; }
QDockWidget { titlebar-close-icon: none; }
QDockWidget::title { background: %PANEL%; padding: 3px 6px; border-bottom: 1px solid %BORDER%; }
QLabel#dockTitle { background: %PANEL%; padding: 3px 6px; border-bottom: 1px solid %BORDER%; color: %TEXT%; }
QToolBar { background: %PANEL%; border: none; border-bottom: 1px solid %BORDER%; spacing: 2px; padding: 2px; }
QToolBar::separator { background: %BORDER%; width: 1px; margin: 4px 4px; }
QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 2px; background: transparent; }
QToolButton:hover { background: %BUTTON%; border-color: %BORDER%; }
QToolButton:checked { background: %ACCENT%; color: %ACCENTTEXT%; border-color: %ACCENT%; }
QToolButton:pressed { background: %BORDER%; }
QToolButton[popupMode="1"] { padding-right: 13px; }
QToolButton::menu-button { border: none; border-left: 1px solid transparent; background: transparent; width: 12px; }
QToolButton::menu-button:hover { border-left-color: %BORDER%; background: %BUTTON%; }
QToolButton::menu-arrow { image: url("%ASSETS%/down.png"); width: 8px; height: 8px; }
QToolButton::menu-indicator { image: url("%ASSETS%/down.png"); width: 8px; height: 8px; subcontrol-origin: padding; subcontrol-position: bottom right; }
QPushButton { background: %BUTTON%; border: 1px solid %BORDER%; border-radius: 3px; padding: 4px 10px; min-height: 18px; }
QPushButton:hover { border-color: %ACCENT%; }
QPushButton:pressed, QPushButton:checked { background: %ACCENT%; color: %ACCENTTEXT%; }
QPushButton:default { border-color: %ACCENT%; }
QPushButton:disabled, QToolButton:disabled { color: %DIM%; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QKeySequenceEdit {
  background: %BASE%; border: 1px solid %BORDER%; border-radius: 3px; padding: 2px 4px; selection-background-color: %ACCENT%; selection-color: %ACCENTTEXT%; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: %ACCENT%; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: %BASE%; border: 1px solid %BORDER%; selection-background-color: %ACCENT%; selection-color: %ACCENTTEXT%; }
QMenuBar { background: %PANEL%; border-bottom: 1px solid %BORDER%; }
QMenuBar::item { padding: 4px 9px; background: transparent; }
QMenuBar::item:selected { background: %BUTTON%; }
QMenu { background: %PANEL%; border: 1px solid %BORDER%; padding: 3px; }
QMenu::item { padding: 4px 26px 4px 22px; border-radius: 3px; }
QMenu::item:selected { background: %ACCENT%; color: %ACCENTTEXT%; }
QMenu::item:disabled { color: %DIM%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 3px 6px; }
QTabWidget::pane { border: 1px solid %BORDER%; top: -1px; }
QTabBar::tab { background: %WINDOW%; border: 1px solid %BORDER%; padding: 4px 10px; margin-right: -1px; }
QTabBar::tab:selected { background: %PANEL%; border-bottom-color: %PANEL%; }
QTabBar::tab:hover { background: %BUTTON%; }
QTreeView, QListView, QListWidget, QTreeWidget, QTableView {
  background: %BASE%; alternate-background-color: %ALT%; border: 1px solid %BORDER%; outline: none;
  selection-background-color: %ACCENT%; selection-color: %ACCENTTEXT%; }
QHeaderView::section { background: %PANEL%; border: none; border-right: 1px solid %BORDER%; padding: 3px 6px; }
QGroupBox { border: 1px solid %BORDER%; border-radius: 4px; margin-top: 10px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %DIM%; }
QSlider::groove:horizontal { height: 4px; background: %BASE%; border: 1px solid %BORDER%; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }
QSlider::handle:horizontal { background: %TEXT%; width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; }
QScrollBar:vertical { background: %WINDOW%; width: 11px; margin: 0; }
QScrollBar:horizontal { background: %WINDOW%; height: 11px; margin: 0; }
QScrollBar::handle { background: %BUTTON%; border: 1px solid %BORDER%; border-radius: 4px; min-height: 24px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QCheckBox::indicator, QRadioButton::indicator, QGroupBox::indicator, QTreeView::indicator, QListView::indicator {
  width: 13px; height: 13px; background: %BASE%; border: 1px solid %CHECKBORDER%; border-radius: 3px; }
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover, QGroupBox::indicator:hover { border-color: %ACCENT%; }
QCheckBox::indicator:checked, QGroupBox::indicator:checked, QTreeView::indicator:checked, QListView::indicator:checked {
  background: %ACCENT%; border-color: %ACCENT%; image: url("%ASSETS%/check.png"); }
QRadioButton::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; image: url("%ASSETS%/dot.png"); }
QCheckBox::indicator:disabled { background: %WINDOW%; border-color: %BORDER%; }
QAbstractSpinBox { min-width: 34px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: border; width: 15px; border: none; background: transparent; }
QAbstractSpinBox::up-button { subcontrol-position: top right; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: %BUTTON%; }
QAbstractSpinBox::up-arrow { image: url("%ASSETS%/up.png"); width: 9px; height: 9px; }
QAbstractSpinBox::down-arrow { image: url("%ASSETS%/down.png"); width: 9px; height: 9px; }
QComboBox::down-arrow { image: url("%ASSETS%/down.png"); width: 9px; height: 9px; }
QTreeView::item, QListView::item { padding: 2px 2px; }
QStatusBar { background: %PANEL%; border-top: 1px solid %BORDER%; }
QStatusBar QLabel { color: %DIM%; padding: 0 6px; }
QToolTip { background: %PANEL%; color: %TEXT%; border: 1px solid %BORDER%; padding: 4px; }
QMdiArea { background: %WINDOW%; }
QLabel#sectionTitle { color: %DIM%; font-weight: 600; padding-top: 4px; }
QFrame#panelFrame { background: %PANEL%; }
)";
    s.replace("%TEXT%", h(c.text)).replace("%WINDOW%", h(c.window)).replace("%PANEL%", h(c.panel));
    s.replace("%BASE%", h(c.base)).replace("%ALT%", h(c.alt)).replace("%BUTTON%", h(c.button));
    s.replace("%BORDER%", h(c.border)).replace("%ACCENT%", h(c.accent)).replace("%ACCENTTEXT%", h(c.accentText));
    s.replace("%DIM%", h(c.dim));
    s.replace("%CHECKBORDER%", h(c.dim));
    s.replace("%ASSETS%", assetsDir(c));
    return s;
}

// ---------------------------------------------------------------------------- icons

void drawIcon(QPainter& p, const QString& n, const QColor& col, const QColor& acc) {
    QPen pen(col, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto line = [&](double a, double b, double c, double d) { p.drawLine(QPointF(a, b), QPointF(c, d)); };
    auto poly = [&](std::initializer_list<QPointF> pts, bool fill = false, const QColor& fc = QColor()) {
        QPolygonF pg(pts);
        p.setBrush(fill ? QBrush(fc.isValid() ? fc : col) : Qt::NoBrush);
        p.drawPolygon(pg);
        p.setBrush(Qt::NoBrush);
    };
    auto circle = [&](double x, double y, double r, bool fill = false, const QColor& fc = QColor()) {
        p.setBrush(fill ? QBrush(fc.isValid() ? fc : col) : Qt::NoBrush);
        p.drawEllipse(QPointF(x, y), r, r);
        p.setBrush(Qt::NoBrush);
    };
    auto dashed = [&](bool on) {
        QPen q = pen;
        if (on) q.setDashPattern({2, 2});
        p.setPen(q);
    };
    auto path = [&](const QPainterPath& pp, bool fill = false, const QColor& fc = QColor()) {
        p.setBrush(fill ? QBrush(fc.isValid() ? fc : col) : Qt::NoBrush);
        p.drawPath(pp);
        p.setBrush(Qt::NoBrush);
    };
    auto plus = [&](double x, double y) {
        QPen q(acc, 2.0, Qt::SolidLine, Qt::RoundCap);
        p.setPen(q);
        line(x - 3.5, y, x + 3.5, y);
        line(x, y - 3.5, x, y + 3.5);
        p.setPen(pen);
    };
    auto nib = [&](bool fillTip) {
        poly({{4, 20}, {6, 14}, {16, 4}, {20, 8}, {10, 18}});
        line(14, 6, 18, 10);
        if (fillTip) poly({{4, 20}, {6, 14}, {10, 18}}, true, acc);
    };
    if (n == "pen") nib(true);
    else if (n == "ink") {
        nib(false);
        QPainterPath d;
        d.moveTo(5, 21.5);
        d.cubicTo(3, 19, 3.5, 17.5, 5, 16);
        d.cubicTo(6.5, 17.5, 7, 19, 5, 21.5);
        path(d, true, acc);
    } else if (n == "pencil") {
        poly({{4, 20}, {5.5, 15}, {15, 5.5}, {18.5, 9}, {9, 18.5}});
        line(5.5, 15, 9, 18.5);
        line(13, 7.5, 16.5, 11);
        poly({{4, 20}, {4.8, 17.5}, {6.5, 19.2}}, true, acc);
    } else if (n == "airbrush") {
        poly({{13, 11}, {19, 5}, {21, 7}, {15, 13}});
        line(12, 12, 16, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(acc);
        for (auto pt : {QPointF(5, 13), QPointF(8, 16), QPointF(4, 17), QPointF(7, 19.5), QPointF(10, 19), QPointF(3.5, 20.5), QPointF(6, 14.5)})
            p.drawEllipse(pt, 1.0, 1.0);
        p.setPen(pen);
    } else if (n == "brush") {
        line(20, 4, 13.5, 10.5);
        poly({{12, 9}, {15, 12}, {13.5, 13.5}, {10.5, 10.5}});
        QPainterPath b;
        b.moveTo(10.5, 11);
        b.cubicTo(7, 11, 7, 15, 5, 17);
        b.cubicTo(4, 18.5, 3, 20.5, 3.5, 21);
        b.cubicTo(6, 21, 10, 19, 12, 16);
        b.cubicTo(13, 14.5, 13, 13.5, 13, 13.5);
        b.closeSubpath();
        path(b, true, acc);
    } else if (n == "watercolor") {
        QPainterPath d;
        d.moveTo(12, 3);
        d.cubicTo(12, 3, 5, 11, 5, 15);
        d.cubicTo(5, 19, 8.2, 21.5, 12, 21.5);
        d.cubicTo(15.8, 21.5, 19, 19, 19, 15);
        d.cubicTo(19, 11, 12, 3, 12, 3);
        QColor a2 = acc;
        a2.setAlpha(150);
        path(d, true, a2);
        line(9, 15.5, 10, 18);
    } else if (n == "marker") {
        poly({{6, 16}, {15, 7}, {19, 11}, {10, 20}});
        poly({{6, 16}, {10, 20}, {4.5, 21.5}, {3, 19}}, true, acc);
        line(15, 7, 17.5, 4.5);
        line(19, 11, 21.5, 8.5);
        line(17.5, 4.5, 21.5, 8.5);
    } else if (n == "chalk") {
        poly({{5, 15}, {14, 6}, {18, 10}, {9, 19}});
        p.setPen(Qt::NoPen);
        p.setBrush(acc);
        for (auto pt : {QPointF(3.5, 19), QPointF(6, 21), QPointF(4, 21.5), QPointF(7.5, 20), QPointF(3, 17)}) p.drawEllipse(pt, 0.9, 0.9);
        p.setPen(pen);
    } else if (n == "bristle") {
        line(20, 4, 14, 10);
        poly({{12.5, 8.5}, {15.5, 11.5}, {14, 13}, {11, 10}});
        p.setPen(QPen(acc, 1.3, Qt::SolidLine, Qt::RoundCap));
        line(11, 11, 4, 18);
        line(12, 12, 5.5, 19.5);
        line(13, 13, 7, 21);
        line(10.5, 12, 3.5, 20.5);
        p.setPen(pen);
    } else if (n == "binary") {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        for (int i = 0; i < 5; ++i) p.drawRect(QRectF(4 + i * 3, 17 - i * 3, 3, 3));
        p.setBrush(acc);
        p.drawRect(QRectF(16, 5, 3, 3));
        p.setPen(pen);
    } else if (n == "eraser" || n == "softeraser") {
        poly({{3, 15}, {12, 6}, {19, 13}, {10, 22}});
        poly({{3, 15}, {7, 11}, {14, 18}, {10, 22}}, true, n == "eraser" ? acc : QColor(acc.red(), acc.green(), acc.blue(), 110));
        line(10, 22, 21, 22);
    } else if (n == "blur") {
        QColor a1 = acc;
        a1.setAlpha(60);
        circle(12, 12, 8.5, true, a1);
        a1.setAlpha(120);
        circle(12, 12, 5.5, true, a1);
        circle(12, 12, 2.5, true, acc);
    } else if (n == "smudge") {
        QPainterPath f;
        f.addRoundedRect(QRectF(9, 3, 7, 13), 3.5, 3.5);
        path(f);
        QPainterPath s;
        s.moveTo(4, 21);
        s.cubicTo(8, 17, 12, 22, 16, 18);
        s.cubicTo(18, 16, 19.5, 17, 20.5, 18);
        QPen q(acc, 2.2, Qt::SolidLine, Qt::RoundCap);
        p.setPen(q);
        p.drawPath(s);
        p.setPen(pen);
    } else if (n == "hatching") {
        // a bundle of lines, one of them in the second color
        for (int i = 0; i < 5; ++i) {
            p.setPen(QPen(i == 3 ? acc : col, 1.3, Qt::SolidLine, Qt::RoundCap));
            line(4 + i * 3.2, 20, 9 + i * 3.2, 4);
        }
        p.setPen(pen);
    } else if (n == "stipple") {
        p.setPen(Qt::NoPen);
        const QPointF pts[] = {{6, 6}, {11, 5}, {17, 7}, {8, 11}, {14, 11}, {19, 13}, {5, 16}, {11, 16}, {16, 18}, {8, 20}, {13, 21}};
        int i = 0;
        for (const QPointF& pt : pts) {
            p.setBrush(i % 4 == 2 ? acc : col);
            p.drawEllipse(pt, 1.2 + (i % 3) * 0.35, 1.2 + (i % 3) * 0.35);
            ++i;
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "charcoal") {
        poly({{5, 16}, {15, 6}, {18.5, 9.5}, {8.5, 19.5}}, true, col);
        p.setPen(QPen(acc, 1.0));
        for (int i = 0; i < 4; ++i) line(3 + i * 1.6, 21 - i * 0.4, 6.5 + i * 1.6, 20.5 - i * 0.4);
        p.setPen(pen);
    } else if (n == "blender") {
        QPainterPath f;
        f.addRoundedRect(QRectF(13, 3, 6, 11), 3, 3);
        path(f);
        QLinearGradient g(QPointF(3, 18), QPointF(20, 18));
        g.setColorAt(0, acc);
        g.setColorAt(1, col);
        p.setPen(QPen(QBrush(g), 3.0, Qt::SolidLine, Qt::RoundCap));
        line(4, 18.5, 19, 18.5);
        p.setPen(pen);
    } else if (n == "glow") {
        QRadialGradient g(QPointF(12, 12), 9);
        QColor a1 = acc;
        g.setColorAt(0, a1);
        a1.setAlpha(0);
        g.setColorAt(1, a1);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        line(12, 3, 12, 6);
        line(12, 18, 12, 21);
        line(3, 12, 6, 12);
        line(18, 12, 21, 12);
    } else if (n == "highlight") {
        QPainterPath h;
        h.moveTo(4, 18);
        h.cubicTo(8, 8, 14, 6, 20, 6);
        path(h);
        p.setPen(QPen(acc, 1.6, Qt::SolidLine, Qt::RoundCap));
        QPainterPath h2;
        h2.moveTo(7, 19);
        h2.cubicTo(10, 12, 15, 10, 20, 10);
        p.drawPath(h2);
        p.setPen(pen);
    } else if (n == "droplet") {
        QPainterPath d;
        d.moveTo(12, 3);
        d.cubicTo(12, 3, 5.5, 11, 5.5, 15);
        d.cubicTo(5.5, 18.8, 8.5, 21.5, 12, 21.5);
        d.cubicTo(15.5, 21.5, 18.5, 18.8, 18.5, 15);
        d.cubicTo(18.5, 11, 12, 3, 12, 3);
        path(d);
        p.setPen(Qt::NoPen);
        p.setBrush(acc);
        p.drawEllipse(QPointF(9.5, 15), 1.6, 2.4);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "ribbon") {
        // a band with a pattern along it
        QPainterPath b;
        b.moveTo(3, 15);
        b.cubicTo(8, 7, 15, 19, 21, 9);
        p.setPen(QPen(col, 6, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(b);
        p.setPen(QPen(acc, 1.4, Qt::DotLine, Qt::RoundCap));
        p.drawPath(b);
        p.setPen(pen);
    } else if (n == "stamp") {
        poly({{8, 4}, {16, 4}, {14, 11}, {10, 11}});
        p.drawRoundedRect(QRectF(5, 11, 14, 4), 1, 1);
        p.setPen(Qt::NoPen);
        p.setBrush(acc);
        p.drawPath([] { QPainterPath s; s.addPolygon(QPolygonF({QPointF(12, 16.5), QPointF(13.3, 19.2), QPointF(16, 19.5), QPointF(14, 21.3), QPointF(14.6, 23.5), QPointF(12, 22.2), QPointF(9.4, 23.5), QPointF(10, 21.3), QPointF(8, 19.5), QPointF(10.7, 19.2)})); return s; }());
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "leaf") {
        QPainterPath l;
        l.moveTo(4, 20);
        l.cubicTo(4, 9, 11, 4, 20, 4);
        l.cubicTo(20, 13, 15, 20, 4, 20);
        path(l, true, acc);
        line(4, 20, 14, 10);
    } else if (n == "rain") {
        p.setPen(QPen(acc, 1.6, Qt::SolidLine, Qt::RoundCap));
        for (auto q : {QLineF(6, 4, 4, 10), QLineF(13, 6, 11, 12), QLineF(19, 3, 17, 9), QLineF(8, 13, 6, 19), QLineF(16, 14, 14, 20)}) p.drawLine(q);
        p.setPen(pen);
    } else if (n == "snow") {
        for (int k = 0; k < 3; ++k) {
            const double a = k * M_PI / 3;
            line(12 - std::cos(a) * 8, 12 - std::sin(a) * 8, 12 + std::cos(a) * 8, 12 + std::sin(a) * 8);
        }
        circle(12, 12, 1.6, true, acc);
    } else if (n == "fur") {
        for (int k = 0; k < 6; ++k) {
            QPainterPath f;
            f.moveTo(4 + k * 3, 21);
            f.quadTo(5 + k * 3, 12, 8 + k * 3 + (k % 2) * 2, 4 + (k % 3));
            p.setPen(QPen(k % 3 == 1 ? acc : col, 1.3, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(f);
        }
        p.setPen(pen);
    } else if (n == "select-rect") {
        dashed(true);
        p.drawRect(QRectF(4, 5, 16, 14));
        dashed(false);
    } else if (n == "select-ellipse") {
        dashed(true);
        p.drawEllipse(QRectF(3.5, 5.5, 17, 13));
        dashed(false);
    } else if (n == "lasso" || n == "lasso-fill") {
        QPainterPath l;
        l.moveTo(7, 16);
        l.cubicTo(1, 12, 6, 3, 13, 4);
        l.cubicTo(21, 5, 22, 13, 15, 15);
        l.cubicTo(11, 16, 8, 16, 7, 16);
        if (n == "lasso-fill") {
            QColor a2 = acc;
            a2.setAlpha(170);
            path(l, true, a2);
        } else {
            dashed(true);
            p.drawPath(l);
            dashed(false);
        }
        QPainterPath t;
        t.moveTo(7, 16);
        t.cubicTo(6, 19, 8, 20, 7, 22);
        p.drawPath(t);
    } else if (n == "magic-wand") {
        line(4, 20, 14, 10);
        p.setPen(QPen(acc, 1.6, Qt::SolidLine, Qt::RoundCap));
        line(17, 3, 17, 7);
        line(15, 5, 19, 5);
        line(20.5, 9, 20.5, 12);
        line(19, 10.5, 22, 10.5);
        line(12, 4.5, 12, 6.5);
        line(11, 5.5, 13, 5.5);
        p.setPen(pen);
    } else if (n == "select-brush") {
        dashed(true);
        circle(12, 12, 8);
        dashed(false);
        circle(12, 12, 3, true, acc);
    } else if (n == "move") {
        line(12, 3, 12, 21);
        line(3, 12, 21, 12);
        poly({{12, 2.5}, {9.5, 5.5}, {14.5, 5.5}}, true);
        poly({{12, 21.5}, {9.5, 18.5}, {14.5, 18.5}}, true);
        poly({{2.5, 12}, {5.5, 9.5}, {5.5, 14.5}}, true);
        poly({{21.5, 12}, {18.5, 9.5}, {18.5, 14.5}}, true);
    } else if (n == "transform") {
        p.drawRect(QRectF(6, 6, 12, 12));
        p.setBrush(acc);
        for (auto pt : {QPointF(6, 6), QPointF(18, 6), QPointF(6, 18), QPointF(18, 18)}) p.drawRect(QRectF(pt.x() - 2, pt.y() - 2, 4, 4));
        p.setBrush(Qt::NoBrush);
    } else if (n == "warp") {
        QPainterPath g;
        for (int i = 0; i < 3; ++i) {
            double y = 5 + i * 7;
            g.moveTo(4, y);
            g.cubicTo(9, y - 3, 15, y + 3, 20, y);
            double x = 5 + i * 7;
            g.moveTo(x, 4);
            g.cubicTo(x + 3, 9, x - 3, 15, x, 20);
        }
        p.drawPath(g);
        circle(12, 12, 1.8, true, acc);
    } else if (n == "liquify") {
        QPainterPath s;
        s.moveTo(12, 12);
        for (int i = 1; i <= 40; ++i) {
            double a = i * 0.32, r = i * 0.22;
            s.lineTo(12 + std::cos(a) * r, 12 + std::sin(a) * r);
        }
        p.drawPath(s);
        circle(12, 12, 1.5, true, acc);
    } else if (n == "fill") {
        poly({{4, 11}, {11, 4}, {18, 11}, {11, 18}});
        line(9, 2, 11, 4);
        QPainterPath d;
        d.moveTo(19.5, 13);
        d.cubicTo(19.5, 13, 17, 16.5, 17, 18);
        d.cubicTo(17, 19.4, 18.1, 20.5, 19.5, 20.5);
        d.cubicTo(20.9, 20.5, 22, 19.4, 22, 18);
        d.cubicTo(22, 16.5, 19.5, 13, 19.5, 13);
        path(d, true, acc);
        poly({{4, 11}, {18, 11}, {11, 18}}, true, QColor(acc.red(), acc.green(), acc.blue(), 140));
    } else if (n == "pattern") {
        p.drawRoundedRect(QRectF(4, 4, 16, 16), 1.5, 1.5);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) circle(8 + x * 8, 8 + y * 8, 1.8, true, (x + y) % 2 ? col : acc);
    } else if (n == "stroke") {
        QPen dash = p.pen();
        dash.setDashPattern({2, 2});
        p.setPen(dash);
        p.drawRect(QRectF(7, 7, 10, 10));
        QPen thick = p.pen();
        thick.setStyle(Qt::SolidLine);
        thick.setColor(acc);
        thick.setWidthF(2.2);
        p.setPen(thick);
        p.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
    } else if (n == "gradient") {
        QLinearGradient g(4, 0, 20, 0);
        g.setColorAt(0, col);
        g.setColorAt(1, QColor(col.red(), col.green(), col.blue(), 0));
        p.setBrush(g);
        p.drawRoundedRect(QRectF(4, 6, 16, 12), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
    } else if (n == "eyedropper") {
        line(5, 19, 13.5, 10.5);
        poly({{12, 9}, {15, 12}, {13.5, 13.5}, {10.5, 10.5}});
        QPainterPath b;
        b.addRoundedRect(QRectF(14, 3, 6, 6), 3, 3);
        QTransform t;
        t.translate(17, 6);
        t.rotate(45);
        t.translate(-17, -6);
        path(t.map(b), true, acc);
        circle(4.5, 19.5, 1.2, true, acc);
    } else if (n == "hand") {
        QPainterPath h;
        h.moveTo(7, 21);
        h.cubicTo(5, 17, 4, 15, 4, 13);
        h.lineTo(6.5, 12);
        h.lineTo(8, 14);
        h.lineTo(8, 5);
        h.cubicTo(8, 3.5, 10.5, 3.5, 10.5, 5);
        h.lineTo(10.5, 11);
        h.lineTo(10.5, 3.5);
        h.cubicTo(10.5, 2, 13, 2, 13, 3.5);
        h.lineTo(13, 11);
        h.lineTo(13, 4.5);
        h.cubicTo(13, 3, 15.5, 3, 15.5, 4.5);
        h.lineTo(15.5, 11.5);
        h.lineTo(15.5, 7);
        h.cubicTo(15.5, 5.5, 18, 5.5, 18, 7);
        h.lineTo(18, 15);
        h.cubicTo(18, 18, 17, 19.5, 16, 21);
        h.closeSubpath();
        path(h);
    } else if (n == "zoom") {
        circle(10, 10, 6);
        p.setPen(QPen(col, 2.6, Qt::SolidLine, Qt::RoundCap));
        line(14.5, 14.5, 20, 20);
        p.setPen(pen);
    } else if (n == "rotate" || n == "reset") {
        QPainterPath a;
        a.arcMoveTo(QRectF(4, 4, 16, 16), 60);
        a.arcTo(QRectF(4, 4, 16, 16), 60, 270);
        p.drawPath(a);
        QPointF tip = QPointF(12 + 8 * std::cos(M_PI / 3), 12 - 8 * std::sin(M_PI / 3));
        poly({tip + QPointF(-1, -3.5), tip + QPointF(3.5, 0.5), tip + QPointF(-2.5, 2.5)}, true);
        if (n == "reset") circle(12, 12, 1.8, true, acc);
    } else if (n == "text") {
        p.setPen(QPen(col, 2.2, Qt::SolidLine, Qt::SquareCap));
        line(5, 5, 19, 5);
        line(12, 5, 12, 20);
        p.setPen(QPen(acc, 1.6));
        line(9, 20, 15, 20);
        p.setPen(pen);
    } else if (n == "vector-pen" || n == "curve" || n == "vector-edit" || n == "vector-pressure" || n == "vector-weight" ||
               n == "vector-color" || n == "vector-eraser") {
        QPainterPath c;
        c.moveTo(3, 18);
        c.cubicTo(8, 4, 14, 22, 21, 6);
        if (n == "vector-weight") {
            QPen q(col, 3.4, Qt::SolidLine, Qt::RoundCap);
            p.setPen(q);
            p.drawPath(c);
            p.setPen(pen);
        } else if (n == "vector-color") {
            QPen q(acc, 2.2, Qt::SolidLine, Qt::RoundCap);
            p.setPen(q);
            p.drawPath(c);
            p.setPen(pen);
        } else {
            p.drawPath(c);
        }
        p.setBrush(acc);
        if (n == "vector-pen" || n == "curve" || n == "vector-edit") {
            for (auto pt : {QPointF(3, 18), QPointF(12, 12.5), QPointF(21, 6)}) p.drawRect(QRectF(pt.x() - 1.8, pt.y() - 1.8, 3.6, 3.6));
        }
        p.setBrush(Qt::NoBrush);
        if (n == "vector-pressure") {
            poly({{18, 15}, {16, 18}, {20, 18}}, true, acc);
            poly({{18, 23}, {16, 20}, {20, 20}}, true, acc);
        }
        if (n == "vector-eraser") {
            p.setPen(QPen(QColor(229, 83, 75), 2, Qt::SolidLine, Qt::RoundCap));
            line(14, 14, 20, 20);
            line(20, 14, 14, 20);
            p.setPen(pen);
        }
    } else if (n == "perspective") {
        line(2, 9, 22, 9);
        for (double x : {3.0, 9.0, 15.0, 21.0}) line(12, 9, x, 21);
        circle(12, 9, 1.8, true, acc);
    } else if (n == "ruler") {
        // a ruler with ticks and a pen line along it
        poly({{3, 15}, {15, 3}, {21, 9}, {9, 21}});
        for (int i = 1; i <= 4; ++i) line(3 + i * 2.4 + 1, 15 - i * 2.4 + 1, 3 + i * 2.4 + (i % 2 ? 3.5 : 2.3), 15 - i * 2.4 + (i % 2 ? 3.5 : 2.3));
        QPen pp = p.pen();
        p.setPen(QPen(acc, 1.8, Qt::SolidLine, Qt::RoundCap));
        line(2, 13, 13, 2);
        p.setPen(pp);
    } else if (n == "focuslines") {
        for (int k = 0; k < 16; ++k) {
            const double a = 2 * M_PI * k / 16, r1 = (k % 2 ? 5.5 : 7.5), r2 = 12;
            line(12 + std::cos(a) * r1, 12 + std::sin(a) * r1, 12 + std::cos(a) * r2, 12 + std::sin(a) * r2);
        }
        circle(12, 12, 2, true, acc);
    } else if (n == "speedlines") {
        for (int k = 0; k < 6; ++k) {
            const double y = 4 + k * 3.2, x0 = 2 + (k * 5) % 7, x1 = 16 + (k * 3) % 6;
            line(x0, y, x1, y);
        }
        poly({{17, 9}, {22, 12}, {17, 15}}, true, acc);
    } else if (n == "symmetry") {
        dashed(true);
        line(12, 2, 12, 22);
        dashed(false);
        poly({{10, 6}, {10, 18}, {3, 16}}, true, acc);
        poly({{14, 6}, {14, 18}, {21, 16}});
    } else if (n == "brushsize") {
        circle(12, 12, 7);
        circle(12, 12, 2.5, true, acc);
        poly({{2, 12}, {5, 10}, {5, 14}}, true);
        poly({{22, 12}, {19, 10}, {19, 14}}, true);
    } else if (n == "eye" || n == "eye-off") {
        QPainterPath e;
        e.moveTo(2.5, 12);
        e.cubicTo(6, 6, 18, 6, 21.5, 12);
        e.cubicTo(18, 18, 6, 18, 2.5, 12);
        path(e);
        circle(12, 12, 3, true);
        if (n == "eye-off") {
            p.setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap));
            line(4, 20, 20, 4);
            p.setPen(pen);
        }
    } else if (n == "lock") {
        QPainterPath s;
        s.moveTo(8, 11);
        s.lineTo(8, 8);
        s.cubicTo(8, 3.5, 16, 3.5, 16, 8);
        s.lineTo(16, 11);
        p.drawPath(s);
        p.setBrush(col);
        p.drawRoundedRect(QRectF(5.5, 11, 13, 10), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
    } else if (n == "folder" || n == "folder-new") {
        poly({{3, 6}, {9, 6}, {11, 8}, {21, 8}, {21, 19}, {3, 19}});
        if (n == "folder-new") plus(18, 17);
    } else if (n == "layer-new" || n == "layer") {
        poly({{12, 4}, {21, 9}, {12, 14}, {3, 9}});
        QPainterPath l;
        l.moveTo(3, 13);
        l.lineTo(12, 18);
        l.lineTo(21, 13);
        p.drawPath(l);
        if (n == "layer-new") plus(18, 19);
    } else if (n == "linework-new") {
        QPainterPath c;
        c.moveTo(3, 16);
        c.cubicTo(8, 4, 13, 18, 17, 7);
        p.drawPath(c);
        plus(18, 18);
    } else if (n == "adjustment") {
        circle(12, 12, 8);
        QPainterPath h;
        h.moveTo(12, 4);
        h.arcTo(QRectF(4, 4, 16, 16), 90, 180);
        h.closeSubpath();
        path(h, true);
    } else if (n == "mask") {
        p.setBrush(col);
        p.drawRoundedRect(QRectF(4, 5, 16, 14), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        circle(12, 12, 4.5, true, acc);
    } else if (n == "duplicate") {
        p.drawRect(QRectF(4, 8, 11, 11));
        p.drawRect(QRectF(9, 4, 11, 11));
    } else if (n == "merge") {
        line(12, 3, 12, 14);
        poly({{12, 16}, {8, 11}, {16, 11}}, true);
        p.setPen(QPen(acc, 2, Qt::SolidLine, Qt::RoundCap));
        line(4, 20, 20, 20);
        p.setPen(pen);
    } else if (n == "trash") {
        line(4, 6, 20, 6);
        poly({{6, 6}, {7, 21}, {17, 21}, {18, 6}});
        line(10, 3.5, 14, 3.5);
        line(10, 10, 10, 17);
        line(14, 10, 14, 17);
    } else if (n == "clear") {
        p.drawRect(QRectF(4, 4, 16, 16));
        p.setPen(QPen(acc, 1.8, Qt::SolidLine, Qt::RoundCap));
        line(8, 8, 16, 16);
        line(16, 8, 8, 16);
        p.setPen(pen);
    } else if (n == "fx") {
        QFont f = p.font();
        f.setPixelSize(13);
        f.setItalic(true);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, "fx");
    } else if (n == "undo" || n == "redo") {
        QPainterPath a;
        if (n == "undo") {
            a.moveTo(7, 9);
            a.lineTo(15, 9);
            a.cubicTo(22, 9, 22, 19, 15, 19);
            a.lineTo(9, 19);
            p.drawPath(a);
            poly({{4, 9}, {8.5, 5.5}, {8.5, 12.5}}, true);
        } else {
            a.moveTo(17, 9);
            a.lineTo(9, 9);
            a.cubicTo(2, 9, 2, 19, 9, 19);
            a.lineTo(15, 19);
            p.drawPath(a);
            poly({{20, 9}, {15.5, 5.5}, {15.5, 12.5}}, true);
        }
    } else if (n == "flip") {
        dashed(true);
        line(12, 2, 12, 22);
        dashed(false);
        poly({{9.5, 6}, {9.5, 18}, {3, 18}}, true);
        poly({{14.5, 6}, {14.5, 18}, {21, 18}});
    } else if (n == "swap") {
        QPainterPath a;
        a.moveTo(5, 9);
        a.cubicTo(5, 4, 15, 4, 17, 7);
        p.drawPath(a);
        poly({{19, 4}, {18.5, 9.5}, {14, 7.5}}, true);
        QPainterPath b;
        b.moveTo(19, 15);
        b.cubicTo(19, 20, 9, 20, 7, 17);
        p.drawPath(b);
        poly({{5, 20}, {5.5, 14.5}, {10, 16.5}}, true);
    } else if (n == "plus") {
        line(12, 5, 12, 19);
        line(5, 12, 19, 12);
    } else if (n == "minus") {
        line(5, 12, 19, 12);
    } else if (n == "settings") {
        for (int i = 0; i < 8; ++i) {
            double a = i * M_PI / 4;
            line(12 + std::cos(a) * 6.5, 12 + std::sin(a) * 6.5, 12 + std::cos(a) * 9.5, 12 + std::sin(a) * 9.5);
        }
        circle(12, 12, 6.5);
        circle(12, 12, 2.5);
    } else if (n == "deselect") {
        dashed(true);
        p.drawRect(QRectF(4, 5, 16, 14));
        dashed(false);
        p.setPen(QPen(QColor(229, 83, 75), 1.8, Qt::SolidLine, Qt::RoundCap));
        line(9, 9, 15, 15);
        line(15, 9, 9, 15);
        p.setPen(pen);
    } else if (n == "invert") {
        dashed(true);
        p.drawRect(QRectF(4, 5, 16, 14));
        dashed(false);
        p.fillRect(QRectF(12, 5, 8, 14), acc);
    } else if (n == "clip") {
        QPainterPath a;
        a.moveTo(6, 4);
        a.lineTo(6, 13);
        a.cubicTo(6, 17, 9, 18, 13, 18);
        a.lineTo(17, 18);
        p.drawPath(a);
        poly({{20, 18}, {16, 14.5}, {16, 21.5}}, true, acc);
    } else if (n == "alpha-lock") {
        p.fillRect(QRectF(4, 4, 6, 6), col);
        p.fillRect(QRectF(10, 10, 6, 6), col);
        p.drawRect(QRectF(4, 4, 12, 12));
        p.setBrush(acc);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(13, 14, 8, 7), 1, 1);
        p.setPen(QPen(acc, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(14.5, 10.5, 5, 6), 0, 180 * 16);
        p.setPen(pen);
    } else if (n == "up") {
        poly({{12, 6}, {5, 16}, {19, 16}}, true);
    } else if (n == "down") {
        poly({{12, 18}, {5, 8}, {19, 8}}, true);
    } else if (n == "menu") {
        line(5, 7, 19, 7);
        line(5, 12, 19, 12);
        line(5, 17, 19, 17);
    } else if (n == "image") {
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        poly({{5, 17}, {10, 11}, {13, 14}, {15, 12}, {19, 17}}, true, acc);
        circle(16, 9, 1.6, true);
    } else if (n == "keyboard") {
        p.drawRoundedRect(QRectF(3, 7, 18, 11), 2, 2);
        for (int i = 0; i < 4; ++i) line(6 + i * 4, 10.5, 6.5 + i * 4, 10.5);
        line(6, 14.5, 6.5, 14.5);
        line(9, 14.5, 15, 14.5);
        line(18, 14.5, 18.5, 14.5);
    } else if (n == "speed") {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(4, 6, 16, 16), 180);
        arc.arcTo(QRectF(4, 6, 16, 16), 180, -180);
        p.drawPath(arc);
        p.setPen(QPen(acc, 2.0, Qt::SolidLine, Qt::RoundCap));
        line(12, 14, 16.5, 9);
        p.setPen(pen);
        circle(12, 14, 1.2, true);
    } else if (n == "cursor") {
        circle(12, 12, 7);
        line(12, 9, 12, 15);
        line(9, 12, 15, 12);
    } else if (n == "language") {
        circle(12, 12, 8);
        p.drawEllipse(QPointF(12, 12), 3.5, 8);
        line(4, 12, 20, 12);
    } else if (n == "theme") {
        circle(12, 12, 8);
        QPainterPath half;
        half.moveTo(12, 4);
        half.arcTo(QRectF(4, 4, 16, 16), 90, 180);
        half.closeSubpath();
        path(half, true);
    } else if (n == "tablet") {
        p.drawRoundedRect(QRectF(3, 5, 18, 13), 2, 2);
        p.drawRect(QRectF(6, 8, 12, 7));
        p.setPen(QPen(acc, 1.8, Qt::SolidLine, Qt::RoundCap));
        line(14, 21, 20, 12);
        p.setPen(pen);
    } else if (n == "channels") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(229, 83, 75, 200));
        p.drawEllipse(QPointF(9.5, 9.5), 5.5, 5.5);
        p.setBrush(QColor(80, 190, 110, 200));
        p.drawEllipse(QPointF(14.5, 9.5), 5.5, 5.5);
        p.setBrush(QColor(80, 140, 240, 200));
        p.drawEllipse(QPointF(12, 14.5), 5.5, 5.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
    } else if (n == "lineart") {
        QPainterPath a;
        a.moveTo(4, 18);
        a.cubicTo(7, 6, 11, 6, 12, 12);
        a.cubicTo(13, 18, 17, 18, 20, 6);
        p.drawPath(a);
        dashed(true);
        p.drawRect(QRectF(3, 3, 18, 18));
        dashed(false);
    } else if (n == "preview") {
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        line(12, 5, 12, 19);
        QPainterPath l;
        l.moveTo(5, 16);
        l.lineTo(8, 10);
        l.lineTo(11, 16);
        path(l, true, acc);
        QPainterPath r;
        r.moveTo(19, 16);
        r.lineTo(16, 10);
        r.lineTo(13, 16);
        p.drawPath(r);
    } else if (n == "timelapse") {
        circle(12, 12, 8);
        line(12, 12, 12, 7);
        line(12, 12, 15.5, 14);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(229, 83, 75));
        p.drawEllipse(QPointF(18.5, 5.5), 2.6, 2.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
    } else if (n == "timelapse-off") {
        circle(12, 12, 8);
        line(12, 12, 12, 7);
        line(12, 12, 15.5, 14);
        p.setPen(QPen(QColor(229, 83, 75), 2.0, Qt::SolidLine, Qt::RoundCap));
        line(5, 19, 19, 5);
        p.setPen(pen);
    } else if (n == "overlay") {
        p.drawRect(QRectF(3, 7, 13, 11));
        p.setBrush(QColor(acc.red(), acc.green(), acc.blue(), 110));
        p.drawRect(QRectF(8, 4, 13, 11));
        p.setBrush(Qt::NoBrush);
    } else if (n == "harmony") {
        circle(12, 12, 8);
        p.setPen(QPen(acc, 1.6));
        p.drawPolygon(QPolygonF({QPointF(12, 4), QPointF(18.9, 16), QPointF(5.1, 16)}));
        p.setPen(pen);
    } else if (n == "reference") {
        QPainterPath a;
        a.moveTo(4, 18);
        a.cubicTo(8, 8, 12, 8, 14, 12);
        p.drawPath(a);
        p.setPen(Qt::NoPen);
        p.setBrush(acc);
        p.drawEllipse(QPointF(18, 6), 3, 3);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        line(15.8, 8.2, 13, 11);
    } else if (n == "grid") {
        for (int i = 0; i <= 3; ++i) {
            line(4 + i * 5.33, 4, 4 + i * 5.33, 20);
            line(4, 4 + i * 5.33, 20, 4 + i * 5.33);
        }
    } else if (n == "check") {
        p.setPen(QPen(acc, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath c;
        c.moveTo(5, 12.5);
        c.lineTo(10, 17.5);
        c.lineTo(19, 6.5);
        p.drawPath(c);
        p.setPen(pen);
    } else if (n == "close") {
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
    } else if (n == "record" || n == "record-on") {
        // always red, so it reads as "record" in every theme; filled while recording
        const QColor red(229, 72, 64);
        p.setPen(QPen(red, 2.0));
        p.setBrush(n == "record-on" ? QBrush(red) : QBrush(QColor(red.red(), red.green(), red.blue(), 70)));
        p.drawEllipse(QPointF(12, 12), 6.5, 6.5);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "pause") {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRoundedRect(QRectF(6.5, 5.5, 3.8, 13), 1, 1);
        p.drawRoundedRect(QRectF(13.7, 5.5, 3.8, 13), 1, 1);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else if (n == "stop") {
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
    } else {
        circle(12, 12, 6);
    }
}

}  // namespace

void Theme::apply(const QString& name, int fontSize) {
    g_name = (name == "light" || name == "gray") ? name : "dark";
    g_colors = makeColors(g_name);
    const ThemeColors& c = g_colors;
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    pal.setColor(QPalette::Window, c.window);
    pal.setColor(QPalette::WindowText, c.text);
    pal.setColor(QPalette::Base, c.base);
    pal.setColor(QPalette::AlternateBase, c.alt);
    pal.setColor(QPalette::ToolTipBase, c.panel);
    pal.setColor(QPalette::ToolTipText, c.text);
    pal.setColor(QPalette::PlaceholderText, c.dim);
    pal.setColor(QPalette::Text, c.text);
    pal.setColor(QPalette::Button, c.button);
    pal.setColor(QPalette::ButtonText, c.text);
    pal.setColor(QPalette::BrightText, Qt::red);
    pal.setColor(QPalette::Highlight, c.accent);
    pal.setColor(QPalette::HighlightedText, c.accentText);
    pal.setColor(QPalette::Link, c.accent);
    pal.setColor(QPalette::Mid, c.border);
    pal.setColor(QPalette::Dark, c.border.darker(130));
    pal.setColor(QPalette::Light, c.button.lighter(120));
    pal.setColor(QPalette::Disabled, QPalette::Text, c.dim);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, c.dim);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, c.dim);
    qApp->setPalette(pal);
    QFont f = qApp->font();
    if (fontSize > 0) f.setPointSize(fontSize);
    qApp->setFont(f);
    qApp->setStyleSheet(css(c));
    clearIconCache();
}

const ThemeColors& Theme::colors() { return g_colors; }
QString Theme::current() { return g_name; }

QPixmap Theme::iconPixmap(const QString& name, int size, const QColor& color) {
    qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    drawIcon(p, name, color, g_colors.accent.isValid() ? g_colors.accent : QColor("#5b9bf0"));
    return pm;
}

namespace {
int g_generation = 0;

// Draws the icon with the colors of the current theme, so icons follow theme switches.
class ThemeIconEngine : public QIconEngine {
public:
    explicit ThemeIconEngine(QString name) : m_name(std::move(name)) {}
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
        QPixmap pm = pixmap(rect.size() * dpr, mode, state);
        pm.setDevicePixelRatio(dpr);
        p->drawPixmap(rect, pm);
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        const quint64 key = (quint64(g_generation) << 40) | (quint64(size.width() & 0xffff) << 16) | (quint64(mode) << 4) | quint64(state);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return *it;
        if (m_cache.size() > 32) m_cache.clear();
        const ThemeColors& c = Theme::colors();
        QColor col = c.text.isValid() ? c.text : QColor(220, 220, 220);
        if (mode == QIcon::Disabled) col = c.dim.isValid() ? c.dim : QColor(140, 140, 140);
        else if (state == QIcon::On || mode == QIcon::Selected) col = c.accentText.isValid() ? c.accentText : QColor(255, 255, 255);
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size.width() / 24.0, size.height() / 24.0);
        QColor acc = c.accent.isValid() ? c.accent : QColor("#5b9bf0");
        if (state == QIcon::On && mode != QIcon::Disabled) acc = col;
        drawIcon(p, m_name, col, acc);
        p.end();
        m_cache.insert(key, pm);
        return pm;
    }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QIconEngine* clone() const override { return new ThemeIconEngine(m_name); }
    QString key() const override { return QStringLiteral("gxtheme"); }
    QString iconName() override { return m_name; }

private:
    QString m_name;
    QHash<quint64, QPixmap> m_cache;
};
}  // namespace

QIcon Theme::icon(const QString& name) {
    auto it = g_icons.find(name);
    if (it != g_icons.end()) return *it;
    QIcon ic(new ThemeIconEngine(name));
    g_icons.insert(name, ic);
    return ic;
}

void Theme::clearIconCache() { ++g_generation; }

}  // namespace gx
