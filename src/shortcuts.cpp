#include "shortcuts.h"

#include "i18n.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const ShortcutDef kDefinitions[] = {
    {"open", "actionOpen", "打开文件", "Open File", "Ctrl+O"},
    {"append", "actionAppend", "添加到播放列表", "Add to Playlist", "Ctrl+Shift+O"},
    {"open-url", "actionOpenUrl", "打开地址", "Open URL", "Ctrl+L"},
    {"play-pause", "actionPlayPause", "播放/暂停", "Play/Pause", "Space"},
    {"previous", "actionPrevious", "上一个", "Previous", "P"},
    {"next", "actionNext", "下一个", "Next", "N"},
    {"seek-back", "actionSeekBack", "后退 5 秒", "Seek Backward 5 Seconds", "Left"},
    {"seek-forward", "actionSeekForward", "前进 5 秒", "Seek Forward 5 Seconds", "Right"},
    {"volume-up", "actionVolumeUp", "音量增加", "Volume Up", "Up"},
    {"volume-down", "actionVolumeDown", "音量减小", "Volume Down", "Down"},
    {"mute", "actionMute", "静音", "Mute", "M"},
    {"playlist", "actionPlaylist", "播放列表", "Playlist", "L"},
    {"fullscreen", "actionFullscreen", "全屏", "Full Screen", "F, F11"},
    {"exit-fullscreen", "actionExitFullscreen", "退出全屏", "Exit Full Screen", "Escape"},
    {"media-info", "actionMediaInfo", "媒体信息", "Media Info", "Ctrl+I"},
    {"quit", "actionQuit", "退出", "Quit", "Ctrl+Q"},
    {"shortcuts", "actionShortcuts", "快捷键", "Shortcuts", ""},
};

QString homeConfigDirectory()
{
    const QByteArray overrideDir = qgetenv("ANIPLAYER_CONFIG_DIR");
    if (!overrideDir.isEmpty())
        return QString::fromLocal8Bit(overrideDir);
    return QDir::homePath() + QStringLiteral("/.config/AniPlayer");
}

} // namespace

QString ShortcutStore::directory()
{
    return homeConfigDirectory();
}

QString ShortcutStore::filePath()
{
    return directory() + QStringLiteral("/shortcuts.conf");
}

QString ShortcutStore::settingsPath()
{
    return directory() + QStringLiteral("/settings.conf");
}

const QList<ShortcutDef> &ShortcutStore::definitions()
{
    static const QList<ShortcutDef> defs = [] {
        QList<ShortcutDef> list;
        for (const ShortcutDef &def : kDefinitions)
            list.append(def);
        return list;
    }();
    return defs;
}

QList<QKeySequence> ShortcutStore::parseList(const QString &text, bool *ok)
{
    QList<QKeySequence> sequences;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        if (ok)
            *ok = true;
        return sequences;
    }
    const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty())
            continue;
        const QKeySequence sequence = QKeySequence::fromString(part, QKeySequence::PortableText);
        if (sequence.isEmpty()) {
            if (ok)
                *ok = false;
            return {};
        }
        sequences.append(sequence);
    }
    if (ok)
        *ok = true;
    return sequences;
}

QString ShortcutStore::portableText(const QList<QKeySequence> &sequences)
{
    QStringList parts;
    for (const QKeySequence &sequence : sequences)
        parts << sequence.toString(QKeySequence::PortableText);
    return parts.join(QStringLiteral(", "));
}

QHash<QString, QList<QKeySequence>> ShortcutStore::defaults()
{
    QHash<QString, QList<QKeySequence>> result;
    for (const ShortcutDef &def : definitions()) {
        bool ok = true;
        result.insert(QString::fromUtf8(def.id), parseList(QString::fromUtf8(def.defaults), &ok));
    }
    return result;
}

QHash<QString, QList<QKeySequence>> ShortcutStore::load(QStringList *warnings)
{
    QHash<QString, QList<QKeySequence>> result = defaults();
    QFile file(filePath());
    if (!file.exists())
        return result;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (warnings)
            warnings->append(ui("无法读取快捷键文件", "Could not read the shortcut file"));
        return result;
    }
    QHash<QString, QList<QKeySequence>> parsed;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    int lineNumber = 0;
    for (QString line : lines) {
        ++lineNumber;
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int split = line.indexOf(QLatin1Char('='));
        if (split <= 0) {
            if (warnings)
                warnings->append(ui("第 %1 行格式无效", "Invalid syntax on line %1").arg(lineNumber));
            continue;
        }
        const QString id = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();
        bool known = false;
        for (const ShortcutDef &def : definitions()) {
            if (id == QLatin1String(def.id)) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (warnings)
                warnings->append(ui("第 %1 行有未知动作 %2", "Unknown action %2 on line %1")
                                     .arg(lineNumber)
                                     .arg(id));
            continue;
        }
        bool ok = true;
        const QList<QKeySequence> sequences = parseList(value, &ok);
        if (!ok) {
            if (warnings)
                warnings->append(ui("第 %1 行的快捷键无法识别：%2", "Unrecognized shortcut on line %1: %2")
                                     .arg(lineNumber)
                                     .arg(value));
            continue;
        }
        parsed[id].append(sequences);
    }
    for (auto it = parsed.cbegin(); it != parsed.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

bool ShortcutStore::save(const QHash<QString, QList<QKeySequence>> &bindings, QString *error)
{
    QDir().mkpath(directory());
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = ui("无法写入 %1", "Could not write %1").arg(filePath());
        return false;
    }
    QByteArray body;
    body += "# AniPlayer shortcuts / 快捷键\n";
    body += "# ";
    body += filePath().toUtf8();
    body += "\n";
    body += "# action = Key, Key\n";
    body += "# Portable text: Ctrl+O, Ctrl+Shift+O, Space, Left, F11, Escape\n";
    body += "# Separate alternative keys with commas. An empty value removes the shortcut.\n";
    body += "# 多个快捷键用逗号分隔。留空表示取消该快捷键。\n";
    body += "# The Shortcuts dialog rewrites this file. Edits here are read on the next launch.\n\n";
    for (const ShortcutDef &def : definitions()) {
        const QString id = QString::fromUtf8(def.id);
        body += "# ";
        body += QString::fromUtf8(def.en).toUtf8();
        body += " / ";
        body += QString::fromUtf8(def.zh).toUtf8();
        body += "\n";
        body += id.toUtf8();
        body += " = ";
        body += portableText(bindings.value(id)).toUtf8();
        body += "\n\n";
    }
    if (file.write(body) != body.size() || !file.commit()) {
        if (error)
            *error = ui("无法写入 %1", "Could not write %1").arg(filePath());
        return false;
    }
    return true;
}

void ShortcutStore::ensureFile()
{
    QDir().mkpath(directory());
    if (!QFileInfo::exists(filePath()))
        save(defaults(), nullptr);
}

void ShortcutStore::apply(QWidget *window, const QHash<QString, QList<QKeySequence>> &bindings)
{
    if (!window)
        return;
    for (const ShortcutDef &def : definitions()) {
        auto *action = window->findChild<QAction *>(QLatin1String(def.actionName));
        if (!action)
            continue;
        const QList<QKeySequence> sequences = bindings.value(QString::fromUtf8(def.id));
        action->setShortcuts(sequences);
    }
}

ShortcutDialog::~ShortcutDialog()
{
    qApp->removeEventFilter(this);
}

ShortcutDialog::ShortcutDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("shortcutDialog"));
    setWindowTitle(ui("快捷键", "Shortcuts"));
    resize(760, 640);
    qApp->installEventFilter(this);

    auto *layout = new QVBoxLayout(this);
    pathLabel_ = new QLabel(this);
    pathLabel_->setObjectName(QStringLiteral("shortcutPath"));
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("shortcutHint"));
    hint_->setWordWrap(true);
    layout->addWidget(hint_);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("shortcutScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("shortcutHost"));
    auto *rows = new QVBoxLayout(host);
    rows->setContentsMargins(8, 8, 8, 8);
    for (const ShortcutDef &def : ShortcutStore::definitions()) {
        const QString id = QString::fromUtf8(def.id);
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(host);
        label->setObjectName(QStringLiteral("shortcutLabel:") + id);
        label->setMinimumWidth(220);
        auto *edit = new QLineEdit(host);
        edit->setObjectName(QStringLiteral("shortcutEdit:") + id);
        edit->setClearButtonEnabled(true);
        auto *record = new QPushButton(host);
        record->setObjectName(QStringLiteral("shortcutRecord:") + id);
        connect(record, &QPushButton::clicked, this, [this, id] {
            capturingId_ = id;
            hint_->setText(ui("请按下要绑定的按键。Esc 也会被记录。",
                              "Press the keys to bind. Escape is recorded too."));
        });
        row->addWidget(label);
        row->addWidget(edit, 1);
        row->addWidget(record);
        rows->addLayout(row);
    }
    rows->addStretch(1);
    scroll->setWidget(host);
    host->setAutoFillBackground(true);
    QPalette scrollPalette = host->palette();
    scrollPalette.setColor(QPalette::Window, QColor(0x23, 0x26, 0x29));
    scrollPalette.setColor(QPalette::WindowText, QColor(0xf4, 0xf6, 0xf8));
    host->setPalette(scrollPalette);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->viewport()->setPalette(scrollPalette);
    layout->addWidget(scroll, 1);

    auto *buttons = new QHBoxLayout;
    auto *openDir = new QPushButton(this);
    openDir->setObjectName(QStringLiteral("shortcutOpenDir"));
    auto *reload = new QPushButton(this);
    reload->setObjectName(QStringLiteral("shortcutReload"));
    auto *reset = new QPushButton(this);
    reset->setObjectName(QStringLiteral("shortcutReset"));
    auto *save = new QPushButton(this);
    save->setObjectName(QStringLiteral("shortcutSave"));
    save->setDefault(true);
    buttons->addWidget(openDir);
    buttons->addWidget(reload);
    buttons->addWidget(reset);
    buttons->addStretch(1);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(openDir, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ShortcutStore::directory()));
    });
    connect(reload, &QPushButton::clicked, this, &ShortcutDialog::reloadFromDisk);
    connect(reset, &QPushButton::clicked, this, &ShortcutDialog::restoreDefaults);
    connect(save, &QPushButton::clicked, this, &ShortcutDialog::saveAndApply);
    connect(&I18n::instance(), &I18n::languageChanged, this, &ShortcutDialog::retranslate);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #232629; color: #f4f6f8; }"
        "QScrollArea#shortcutScroll, QWidget#shortcutHost { background: #232629; }"
        "QLabel { color: #f4f6f8; background: transparent; }"
        "QLabel#shortcutHint { color: #b7c0c7; }"
        "QLineEdit { background: #1b1e20; color: #eff0f1; border: 1px solid #3a4044; "
        "border-radius: 4px; padding: 4px 6px; }"
        "QPushButton { background: #31363b; color: #eff0f1; border: none; border-radius: 6px; "
        "padding: 6px 12px; }"
        "QPushButton:hover { background: #3d444a; }"
        "QPushButton#shortcutSave { background: #3daee9; color: #172026; font-weight: 600; }"));

    retranslate();
    reloadFromDisk();
}

bool ShortcutDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!capturingId_.isEmpty() && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int code = key->key();
        if (code == Qt::Key_Control || code == Qt::Key_Shift || code == Qt::Key_Alt || code == Qt::Key_Meta
            || code == Qt::Key_unknown)
            return true;
        Qt::KeyboardModifiers mods = key->modifiers();
        mods &= Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
        const QKeySequence sequence(mods | static_cast<Qt::Key>(code));
        if (auto *edit = findChild<QLineEdit *>(QStringLiteral("shortcutEdit:") + capturingId_))
            edit->setText(sequence.toString(QKeySequence::PortableText));
        capturingId_.clear();
        hint_->setText(ui("已记录。多个快捷键用逗号分隔，例如 F, F11。",
                          "Recorded. Separate alternative shortcuts with commas, for example F, F11."));
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void ShortcutDialog::reloadFromDisk()
{
    QStringList warnings;
    fillEditors(ShortcutStore::load(&warnings));
    hint_->setText(warnings.isEmpty()
                       ? ui("改完后点保存，会写回配置文件。下次启动也会读取它。",
                            "Save writes the config file. It is read again on the next launch.")
                       : warnings.join(QStringLiteral("\n")));
}

void ShortcutDialog::fillEditors(const QHash<QString, QList<QKeySequence>> &bindings)
{
    for (const ShortcutDef &def : ShortcutStore::definitions()) {
        const QString id = QString::fromUtf8(def.id);
        if (auto *edit = findChild<QLineEdit *>(QStringLiteral("shortcutEdit:") + id))
            edit->setText(ShortcutStore::portableText(bindings.value(id)));
    }
}

void ShortcutDialog::retranslate()
{
    setWindowTitle(ui("快捷键", "Shortcuts"));
    pathLabel_->setText(ui("配置文件：%1", "Config file: %1").arg(ShortcutStore::filePath()));
    if (auto *openDir = findChild<QPushButton *>(QStringLiteral("shortcutOpenDir")))
        openDir->setText(ui("打开配置目录", "Open Config Folder"));
    if (auto *reload = findChild<QPushButton *>(QStringLiteral("shortcutReload")))
        reload->setText(ui("重新读取", "Reload"));
    if (auto *reset = findChild<QPushButton *>(QStringLiteral("shortcutReset")))
        reset->setText(ui("恢复默认", "Restore Defaults"));
    if (auto *save = findChild<QPushButton *>(QStringLiteral("shortcutSave")))
        save->setText(ui("保存", "Save"));
    for (const ShortcutDef &def : ShortcutStore::definitions()) {
        const QString id = QString::fromUtf8(def.id);
        if (auto *label = findChild<QLabel *>(QStringLiteral("shortcutLabel:") + id))
            label->setText(ui(def.zh, def.en));
        if (auto *record = findChild<QPushButton *>(QStringLiteral("shortcutRecord:") + id))
            record->setText(ui("录制", "Record"));
    }
}

bool ShortcutDialog::collect(QHash<QString, QList<QKeySequence>> *bindings, QString *error)
{
    QHash<QString, QString> owners;
    bindings->clear();
    for (const ShortcutDef &def : ShortcutStore::definitions()) {
        const QString id = QString::fromUtf8(def.id);
        auto *edit = findChild<QLineEdit *>(QStringLiteral("shortcutEdit:") + id);
        if (!edit)
            continue;
        bool ok = true;
        const QList<QKeySequence> sequences = ShortcutStore::parseList(edit->text(), &ok);
        if (!ok) {
            if (error)
                *error = ui("%1 的快捷键无法识别：%2", "Unrecognized shortcut for %1: %2")
                             .arg(ui(def.zh, def.en), edit->text());
            edit->setFocus();
            return false;
        }
        for (const QKeySequence &sequence : sequences) {
            const QString key = sequence.toString(QKeySequence::PortableText);
            if (owners.contains(key)) {
                if (error) {
                    *error = ui("“%1”和“%2”都使用了 %3", "“%1” and “%2” both use %3")
                                 .arg(ui(def.zh, def.en), owners.value(key), key);
                }
                return false;
            }
            owners.insert(key, ui(def.zh, def.en));
        }
        bindings->insert(id, sequences);
    }
    return true;
}

void ShortcutDialog::saveAndApply()
{
    QHash<QString, QList<QKeySequence>> bindings;
    QString error;
    if (!collect(&bindings, &error)) {
        QMessageBox::warning(this, ui("快捷键", "Shortcuts"), error);
        return;
    }
    if (!ShortcutStore::save(bindings, &error)) {
        QMessageBox::warning(this, ui("快捷键", "Shortcuts"), error);
        return;
    }
    ShortcutStore::apply(parentWidget(), bindings);
    accept();
}

void ShortcutDialog::restoreDefaults()
{
    const auto bindings = ShortcutStore::defaults();
    QString error;
    if (!ShortcutStore::save(bindings, &error)) {
        QMessageBox::warning(this, ui("快捷键", "Shortcuts"), error);
        return;
    }
    fillEditors(bindings);
    ShortcutStore::apply(parentWidget(), bindings);
    hint_->setText(ui("已恢复默认并写入配置文件。", "Defaults were restored and written to the config file."));
}
