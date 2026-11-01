#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

struct MediaField {
    QString label;
    QString value;
};

struct MediaSection {
    QString heading;
    QVector<MediaField> fields;
};

struct MediaDetails {
    QString error;
    QString rawJson;
    QVector<MediaField> container;
    QVector<MediaSection> videos;
    QVector<MediaSection> audios;
    QVector<MediaSection> others;
    QVector<MediaField> chapters;
};

class MediaProbe {
public:
    static QString executable();
    static MediaDetails inspect(const QString &mediaPath, int timeoutMs = 20000);
};

class MediaInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaInfoDialog(QWidget *parent = nullptr);
    void probe(const QString &mediaPath);

private:
    void showDetails(const MediaDetails &details);
    class QLabel *status_ = nullptr;
    class QTabWidget *tabs_ = nullptr;
    class QWidget *overview_ = nullptr;
    class QPlainTextEdit *raw_ = nullptr;
    QString lastPath_;
};
