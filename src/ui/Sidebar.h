#pragma once

#include <QListWidget>

class Library;

// Rail de gauche : catégories (tous, favoris, récents) puis une entrée par console.
class Sidebar : public QListWidget {
    Q_OBJECT
public:
    explicit Sidebar(Library* lib, QWidget* parent = nullptr);

    void rebuild();          // recrée les entrées (registre modifié)
    void refreshCounts();    // met à jour les compteurs
    QString currentCategory() const;
    void selectCategory(const QString& id);
    void step(int delta);    // catégorie précédente / suivante (manette)

signals:
    void categoryChanged(const QString& id);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void fitRows();
    Library* m_lib;
};
