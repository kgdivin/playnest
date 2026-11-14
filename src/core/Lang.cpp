#include "Lang.h"

#include "Settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSet>
#include <QTranslator>

namespace {

struct Entry {
    QString one;   // forme pour n == 1 (ou seule forme)
    QString other; // forme plurielle
};

class DictTranslator : public QTranslator {
public:
    bool load(const QString& resource) {
        QFile f(resource);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_dict.reserve(o.size());
        for (auto it = o.begin(); it != o.end(); ++it) {
            Entry e;
            if (it.value().isObject()) {
                e.one = it.value().toObject().value(QStringLiteral("one")).toString();
                e.other = it.value().toObject().value(QStringLiteral("other")).toString();
            } else {
                e.one = e.other = it.value().toString();
            }
            m_dict.insert(it.key(), e);
        }
        return !m_dict.isEmpty();
    }
    bool isEmpty() const override { return m_dict.isEmpty(); }
    QString translate(const char*, const char* sourceText, const char*, int n) const override {
        const QString src = QString::fromUtf8(sourceText);
        const auto it = m_dict.constFind(src);
        if (it == m_dict.constEnd()) {
            noteMissing(src);
            return {};
        }
        return n == 1 ? it->one : it->other;
    }

private:
    // Aide à la traduction : PLAYNEST_LANG_MISSING=fichier liste les textes sans traduction rencontrés.
    void noteMissing(const QString& src) const {
        static const QString out = qEnvironmentVariable("PLAYNEST_LANG_MISSING");
        if (out.isEmpty() || src.trimmed().isEmpty())
            return;
        static QMutex mutex;
        static QSet<QString> seen;
        QMutexLocker lock(&mutex);
        if (seen.contains(src))
            return;
        seen.insert(src);
        QFile f(out);
        if (f.open(QIODevice::Append))
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("s"), src}}).toJson(QJsonDocument::Compact) + '\n');
    }
    QHash<QString, Entry> m_dict;
};

DictTranslator* g_translator = nullptr;
QString g_current = QStringLiteral("fr");

void apply(const QString& lang) {
    if (g_translator) {
        QCoreApplication::removeTranslator(g_translator);
        delete g_translator;
        g_translator = nullptr;
    }
    g_current = lang == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("fr");
    if (g_current == QLatin1String("en")) {
        g_translator = new DictTranslator;
        if (g_translator->load(QStringLiteral(":/lang/en.json")))
            QCoreApplication::installTranslator(g_translator);
    }
    QLocale::setDefault(Lang::locale());
}

} // namespace

namespace Lang {

QString systemLanguage() {
    for (const QString& ui : QLocale::system().uiLanguages())
        if (ui.startsWith(QLatin1String("fr"), Qt::CaseInsensitive))
            return QStringLiteral("fr");
        else if (!ui.isEmpty())
            break; // la première langue de Windows décide
    return QLocale::system().language() == QLocale::French ? QStringLiteral("fr") : QStringLiteral("en");
}

QString setting() {
    const QString s = Settings::instance().value(QStringLiteral("ui/language"), QStringLiteral("auto")).toString();
    return (s == QLatin1String("fr") || s == QLatin1String("en")) ? s : QStringLiteral("auto");
}

void init() {
    QString lang = qEnvironmentVariable("PLAYNEST_LANG");
    if (lang != QLatin1String("fr") && lang != QLatin1String("en"))
        lang = setting();
    apply(lang == QLatin1String("auto") ? systemLanguage() : lang);
}

QString current() { return g_current; }

void setLanguage(const QString& l) {
    Settings::instance().setValue(QStringLiteral("ui/language"), l);
    apply(l == QLatin1String("auto") ? systemLanguage() : l);
}

QLocale locale() { return g_current == QLatin1String("en") ? QLocale(QLocale::English, QLocale::UnitedStates) : QLocale(QLocale::French, QLocale::France); }

QString t(const QString& french) {
    if (french.isEmpty() || !g_translator)
        return french;
    const QString out = g_translator->translate("data", french.toUtf8().constData(), nullptr, -1);
    return out.isEmpty() ? french : out;
}

} // namespace Lang
