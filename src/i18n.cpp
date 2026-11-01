#include "i18n.h"

#include <QLocale>

I18n &I18n::instance()
{
    static I18n i18n;
    return i18n;
}

I18n::I18n()
    : language_(systemDefault())
{
}

I18n::Language I18n::systemDefault()
{
    return QLocale::system().language() == QLocale::Chinese ? Language::Chinese : Language::English;
}

void I18n::setLanguage(Language language)
{
    if (language_ == language)
        return;
    language_ = language;
    emit languageChanged();
}

QString I18n::text(const char *zh, const char *en)
{
    return QString::fromUtf8(instance().language_ == Language::Chinese ? zh : en);
}
