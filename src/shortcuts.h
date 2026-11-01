#pragma once

#include <QDialog>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;
class QWidget;

struct ShortcutDef {
    const char *id;
    const char *actionName;
    const char *zh;
    const char *en;
    const char *defaults;
};

class ShortcutStore {
public:
    static QString directory();
    static QString filePath();
    static QString settingsPath();
    static const QList<ShortcutDef> &definitions();
    static QHash<QString, QList<QKeySequence>> defaults();
    static QHash<QString, QList<QKeySequence>> load(QStringList *warnings = nullptr);
    static bool save(const QHash<QString, QList<QKeySequence>> &bindings, QString *error);
    static void ensureFile();
    static void apply(QWidget *window, const QHash<QString, QList<QKeySequence>> &bindings);
    static QList<QKeySequence> parseList(const QString &text, bool *ok = nullptr);
    static QString portableText(const QList<QKeySequence> &sequences);
};

class ShortcutDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutDialog(QWidget *parent = nullptr);
    ~ShortcutDialog() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reloadFromDisk();
    void fillEditors(const QHash<QString, QList<QKeySequence>> &bindings);
    void retranslate();
    bool collect(QHash<QString, QList<QKeySequence>> *bindings, QString *error);
    void saveAndApply();
    void restoreDefaults();

    QString capturingId_;
    class QLabel *hint_ = nullptr;
    class QLabel *pathLabel_ = nullptr;
};
