#include "ui/ThemeEditor.h"

#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

#include "app/App.h"
#include "app/I18n.h"
#include "ui/Widgets.h"

namespace gx {

ThemeEditorDialog::ThemeEditorDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(T("My theme"));
    m_before = app()->prefs().theme;
    m_c = Theme::customColors();
    auto* v = new QVBoxLayout(this);
    auto* info = new QLabel(T("Every change shows on the whole program right away. Text that would be hard to read is made black or white "
                              "by itself. Cancel brings the theme back as it was."),
                            this);
    info->setWordWrap(true);
    info->setObjectName("sectionTitle");
    v->addWidget(info);
    auto* startRow = new QHBoxLayout;
    startRow->addWidget(new QLabel(T("Start from"), this));
    auto* start = new QComboBox(this);
    start->addItem(T("(keep)"), QString());
    for (const QString& n : Theme::names())
        if (n != "custom") start->addItem(T(Theme::displayName(n)), n);
    startRow->addWidget(start, 1);
    v->addLayout(startRow);
    connect(start, &QComboBox::activated, this, [this, start](int i) {
        const QString n = start->itemData(i).toString();
        if (n.isEmpty()) return;
        const ThemeColors from = Theme::colorsFor(n);
        // the colors and roundness of that theme; the picture stays
        const QString tex = m_c.texture;
        const double to = m_c.textureOpacity, wo = m_c.windowTextureOpacity;
        m_c = from;
        m_c.texture = tex;
        m_c.textureOpacity = to;
        m_c.windowTextureOpacity = wo;
        rebuild();
        preview();
    });
    m_body = new QWidget(this);
    v->addWidget(m_body, 1);
    rebuild();

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* exp = bb->addButton(T("Export..."), QDialogButtonBox::ActionRole);
    auto* imp = bb->addButton(T("Import..."), QDialogButtonBox::ActionRole);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, T("Export theme"), "my theme.gxtheme", "GraphiX theme (*.gxtheme)");
        QString err;
        if (!f.isEmpty() && !exportTheme(m_c, f, &err)) QMessageBox::warning(this, T("Export theme"), err);
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, T("Import theme"), {}, "GraphiX theme (*.gxtheme);;All files (*)");
        if (f.isEmpty()) return;
        QString err;
        ThemeColors c;
        if (!importTheme(f, &c, &err)) {
            QMessageBox::warning(this, T("Import theme"), err);
            return;
        }
        m_c = c;
        rebuild();
        preview();
    });
    v->addWidget(bb);
    resize(520, 640);
    preview();
}

void ThemeEditorDialog::rebuild() {
    // the controls are made again so they show m_c
    delete m_body->layout();
    for (QWidget* w : m_body->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) w->deleteLater();
    auto* v = new QVBoxLayout(m_body);
    v->setContentsMargins(0, 0, 0, 0);
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(4);
    const QList<QPair<QString, QColor*>> colors = {
        {T("Window background"), &m_c.window}, {T("Panels"), &m_c.panel},          {T("Fields and lists"), &m_c.base},
        {T("Every other row"), &m_c.alt},     {T("Buttons"), &m_c.button},        {T("Lines and borders"), &m_c.border},
        {T("Text"), &m_c.text},               {T("Quiet text"), &m_c.dim},        {T("Accent"), &m_c.accent},
        {T("Text on the accent"), &m_c.accentText}, {T("Around the canvas"), &m_c.workspace}};
    for (int i = 0; i < colors.size(); ++i) {
        auto* lab = new QLabel(colors[i].first, m_body);
        auto* b = new ColorButton(*colors[i].second, m_body);
        QColor* field = colors[i].second;
        connect(b, &ColorButton::colorChanged, this, [this, field](const QColor& c) {
            *field = c;
            preview();
        });
        g->addWidget(lab, i / 2, (i % 2) * 2);
        g->addWidget(b, i / 2, (i % 2) * 2 + 1);
    }
    v->addLayout(g);
    auto* round = new ValueBar(T("Roundness"), 0, 16, 0, m_body);
    round->setSuffix(" px");
    round->setValue(m_c.radius);
    connect(round, &ValueBar::valueChanged, this, [this](double x) {
        m_c.radius = int(x);
        preview();
    });
    v->addWidget(round);

    auto* t = new QLabel(T("Picture over the panels"), m_body);
    t->setObjectName("sectionTitle");
    v->addWidget(t);
    auto* row = new QHBoxLayout;
    auto* pick = new QPushButton(T("Choose a picture..."), m_body);
    auto* none = new QPushButton(T("No picture"), m_body);
    m_texName = new QLabel(m_c.texture.isEmpty() ? T("(none)") : QFileInfo(m_c.texture).fileName(), m_body);
    row->addWidget(pick);
    row->addWidget(none);
    row->addWidget(m_texName, 1);
    v->addLayout(row);
    connect(pick, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, T("Choose a picture"), {}, T("Pictures (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (f.isEmpty()) return;
        QImage img(f);
        if (img.isNull()) return;
        // a copy in the program's folder, so the theme keeps working if the file moves
        const QString dir = app()->dataDir() + "/themes";
        QDir().mkpath(dir);
        const QString copy = dir + "/" + QFileInfo(f).completeBaseName() + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".png";
        if (img.width() > 1024 || img.height() > 1024) img = img.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        img.save(copy);
        m_c.texture = copy;
        if (m_c.textureOpacity <= 0) m_c.textureOpacity = 0.25;
        m_texName->setText(QFileInfo(f).fileName());
        rebuild();
        preview();
    });
    connect(none, &QPushButton::clicked, this, [this] {
        m_c.texture.clear();
        m_texName->setText(T("(none)"));
        preview();
    });
    auto* po = new ValueBar(T("Over the panels"), 0, 1, 2, m_body);
    po->setDisplayScale(100);
    po->setSuffix("%");
    po->setValue(m_c.textureOpacity);
    connect(po, &ValueBar::valueChanged, this, [this](double x) {
        m_c.textureOpacity = x;
        preview();
    });
    v->addWidget(po);
    auto* wo = new ValueBar(T("Over the window background"), 0, 1, 2, m_body);
    wo->setDisplayScale(100);
    wo->setSuffix("%");
    wo->setValue(m_c.windowTextureOpacity);
    connect(wo, &ValueBar::valueChanged, this, [this](double x) {
        m_c.windowTextureOpacity = x;
        preview();
    });
    v->addWidget(wo);
    v->addStretch();
}

void ThemeEditorDialog::preview() {
    Preferences& p = app()->prefs();
    p.theme = "custom";  // so the canvas shows "around the canvas" too
    Theme::applyColors(m_c, p.uiFontSize);
    app()->preferencesChanged();
}

void ThemeEditorDialog::accept() {
    Theme::saveCustom(m_c);
    app()->prefs().theme = "custom";
    Theme::apply("custom", app()->prefs().uiFontSize);
    app()->prefs().save(app()->settings());
    app()->preferencesChanged();
    QDialog::accept();
}

void ThemeEditorDialog::reject() {
    app()->prefs().theme = m_before;
    Theme::apply(m_before, app()->prefs().uiFontSize);
    app()->preferencesChanged();
    QDialog::reject();
}

bool ThemeEditorDialog::exportTheme(const ThemeColors& c, const QString& file, QString* error) {
    QJsonObject o = c.toJson();
    o.remove("texture");
    if (!c.texture.isEmpty()) {
        // the picture goes inside the file
        QImage img(c.texture);
        if (!img.isNull()) {
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            o["textureData"] = QString::fromLatin1(png.toBase64());
        }
    }
    o["format"] = "GraphiX theme";
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson());
    return true;
}

bool ThemeEditorDialog::importTheme(const QString& file, ThemeColors* out, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.value("format").toString() != "GraphiX theme") {
        if (error) *error = T("This is not a GraphiX theme file.");
        return false;
    }
    ThemeColors c = ThemeColors::fromJson(o);
    c.texture.clear();
    const QByteArray png = QByteArray::fromBase64(o.value("textureData").toString().toLatin1());
    if (!png.isEmpty()) {
        QImage img;
        if (img.loadFromData(png, "PNG")) {
            const QString dir = app()->dataDir() + "/themes";
            QDir().mkpath(dir);
            const QString path = dir + "/" + QFileInfo(file).completeBaseName() + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".png";
            img.save(path);
            c.texture = path;
        }
    }
    *out = c;
    return true;
}

}  // namespace gx
