#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTimer;

// Navigation à la manette dans l'interface.
// Windows : XInput (manettes Xbox et compatibles, chargé dynamiquement).
// Linux : SDL2 si disponible à la compilation (PLAYNEST_HAVE_SDL2).
class Gamepad : public QObject {
    Q_OBJECT
public:
    enum Action { Up, Down, Left, Right, Accept, Back, PrevCategory, NextCategory, Favorite, Menu, Options, PageUp, PageDown };
    Q_ENUM(Action)

    explicit Gamepad(QObject* parent = nullptr);
    ~Gamepad() override;

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }
    bool isConnected() const { return m_connected; }
    QString backendName() const;

signals:
    void action(Gamepad::Action a);
    void connectionChanged(bool connected);

private:
    void poll();
    void handleButtons(quint32 buttons);

    QTimer* m_timer = nullptr;
    bool m_enabled = false;
    bool m_connected = false;
    quint32 m_prev = 0;
    quint32 m_heldDir = 0;
    QElapsedTimer m_repeat;
    bool m_repeating = false;
    int m_noPadTicks = 0;
    void* m_backend = nullptr;
};
