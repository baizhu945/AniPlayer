#pragma once

#include <QObject>
#include <QString>

class I18n : public QObject {
    Q_OBJECT
public:
    enum class Language { Chinese, English };

    static I18n &instance();
    Language language() const { return language_; }
    void setLanguage(Language language);
    static QString text(const char *zh, const char *en);
    static Language systemDefault();

signals:
    void languageChanged();

private:
    I18n();
    Language language_ = Language::Chinese;
};

inline QString ui(const char *zh, const char *en)
{
    return I18n::text(zh, en);
}
