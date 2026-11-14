#pragma once

#include <QLocale>
#include <QString>

// Langue de l'interface. Les textes du code sont en français ; l'anglais vient d'un dictionnaire
// (resources/lang/en.json, « texte français » → « texte anglais ») installé comme traducteur Qt.
namespace Lang {
void init();                        // à appeler après Settings::init()
QString current();                  // "fr" ou "en"
QString setting();                  // "auto", "fr" ou "en"
void setLanguage(const QString& l); // enregistre le choix et l'applique aux textes créés ensuite
QLocale locale();                   // dates et tailles dans la langue de l'interface
QString t(const QString& french);   // traduit un texte venu des fichiers JSON (notes, prérequis…)
QString systemLanguage();           // "fr" si Windows est en français, sinon "en"
} // namespace Lang
