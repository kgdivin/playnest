#include "Gamepad.h"

#include <QTimer>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(PLAYNEST_HAVE_SDL2)
#include <SDL.h>
#endif

namespace {

// Masque de boutons interne (indépendant du backend).
enum : quint32 {
    B_UP = 1u << 0, B_DOWN = 1u << 1, B_LEFT = 1u << 2, B_RIGHT = 1u << 3,
    B_A = 1u << 4, B_B = 1u << 5, B_X = 1u << 6, B_Y = 1u << 7,
    B_LB = 1u << 8, B_RB = 1u << 9, B_START = 1u << 10, B_BACK = 1u << 11,
    B_LT = 1u << 12, B_RT = 1u << 13,
    DIR_MASK = B_UP | B_DOWN | B_LEFT | B_RIGHT,
};

constexpr int kStickDeadzone = 16000;
constexpr int kTriggerThreshold = 120;

#if defined(Q_OS_WIN)
struct XGamepad { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger; SHORT sThumbLX; SHORT sThumbLY; SHORT sThumbRX; SHORT sThumbRY; };
struct XState { DWORD dwPacketNumber; XGamepad Gamepad; };
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XState*);

struct Backend {
    HMODULE lib = nullptr;
    XInputGetStateFn getState = nullptr;
    Backend() {
        for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
            lib = LoadLibraryW(name);
            if (lib) break;
        }
        if (lib)
            getState = reinterpret_cast<XInputGetStateFn>(reinterpret_cast<void*>(GetProcAddress(lib, "XInputGetState")));
    }
    ~Backend() { if (lib) FreeLibrary(lib); }
    // Renvoie false si aucune manette n'est branchée.
    bool read(quint32& out) {
        if (!getState) return false;
        for (DWORD i = 0; i < 4; ++i) {
            XState st{};
            if (getState(i, &st) != 0) continue;
            const WORD b = st.Gamepad.wButtons;
            quint32 m = 0;
            if (b & 0x0001) m |= B_UP;
            if (b & 0x0002) m |= B_DOWN;
            if (b & 0x0004) m |= B_LEFT;
            if (b & 0x0008) m |= B_RIGHT;
            if (b & 0x0010) m |= B_START;
            if (b & 0x0020) m |= B_BACK;
            if (b & 0x0100) m |= B_LB;
            if (b & 0x0200) m |= B_RB;
            if (b & 0x1000) m |= B_A;
            if (b & 0x2000) m |= B_B;
            if (b & 0x4000) m |= B_X;
            if (b & 0x8000) m |= B_Y;
            if (st.Gamepad.sThumbLY > kStickDeadzone) m |= B_UP;
            if (st.Gamepad.sThumbLY < -kStickDeadzone) m |= B_DOWN;
            if (st.Gamepad.sThumbLX < -kStickDeadzone) m |= B_LEFT;
            if (st.Gamepad.sThumbLX > kStickDeadzone) m |= B_RIGHT;
            if (st.Gamepad.bLeftTrigger > kTriggerThreshold) m |= B_LT;
            if (st.Gamepad.bRightTrigger > kTriggerThreshold) m |= B_RT;
            out = m;
            return true;
        }
        return false;
    }
    static const char* name() { return "XInput"; }
};
#elif defined(PLAYNEST_HAVE_SDL2)
struct Backend {
    SDL_GameController* pad = nullptr;
    bool ok = false;
    Backend() { ok = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0; }
    ~Backend() {
        if (pad) SDL_GameControllerClose(pad);
        if (ok) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    bool read(quint32& out) {
        if (!ok) return false;
        SDL_GameControllerUpdate();
        if (pad && !SDL_GameControllerGetAttached(pad)) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
        }
        if (!pad) {
            for (int i = 0; i < SDL_NumJoysticks(); ++i)
                if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i)))
                    break;
            if (!pad) return false;
        }
        auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
        quint32 m = 0;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) m |= B_UP;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) m |= B_DOWN;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) m |= B_LEFT;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= B_RIGHT;
        if (btn(SDL_CONTROLLER_BUTTON_A)) m |= B_A;
        if (btn(SDL_CONTROLLER_BUTTON_B)) m |= B_B;
        if (btn(SDL_CONTROLLER_BUTTON_X)) m |= B_X;
        if (btn(SDL_CONTROLLER_BUTTON_Y)) m |= B_Y;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) m |= B_LB;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= B_RB;
        if (btn(SDL_CONTROLLER_BUTTON_START)) m |= B_START;
        if (btn(SDL_CONTROLLER_BUTTON_BACK)) m |= B_BACK;
        const int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        const int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ly < -kStickDeadzone) m |= B_UP;
        if (ly > kStickDeadzone) m |= B_DOWN;
        if (lx < -kStickDeadzone) m |= B_LEFT;
        if (lx > kStickDeadzone) m |= B_RIGHT;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > kTriggerThreshold * 128) m |= B_LT;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > kTriggerThreshold * 128) m |= B_RT;
        out = m;
        return true;
    }
    static const char* name() { return "SDL2"; }
};
#else
struct Backend {
    bool read(quint32&) { return false; }
    static const char* name() { return "aucun"; }
};
#endif

} // namespace

Gamepad::Gamepad(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &Gamepad::poll);
}

Gamepad::~Gamepad() { delete static_cast<Backend*>(m_backend); }

QString Gamepad::backendName() const { return QString::fromLatin1(Backend::name()); }

void Gamepad::setEnabled(bool on) {
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on) {
        if (!m_backend)
            m_backend = new Backend();
        m_prev = 0;
        m_timer->start();
    } else {
        m_timer->stop();
        if (m_connected) {
            m_connected = false;
            emit connectionChanged(false);
        }
    }
}

void Gamepad::poll() {
    auto* be = static_cast<Backend*>(m_backend);
    quint32 buttons = 0;
    const bool present = be && be->read(buttons);
    if (present != m_connected) {
        m_connected = present;
        emit connectionChanged(present);
    }
    if (!present) {
        // Sans manette, on ralentit la scrutation pour économiser le CPU.
        if (++m_noPadTicks > 120)
            m_timer->setInterval(500);
        m_prev = 0;
        return;
    }
    m_noPadTicks = 0;
    if (m_timer->interval() != 16)
        m_timer->setInterval(16);
    handleButtons(buttons);
}

void Gamepad::handleButtons(quint32 b) {
    const quint32 pressed = b & ~m_prev;
    m_prev = b;

    // Directions avec répétition automatique (350 ms puis toutes les 80 ms).
    const quint32 dir = b & DIR_MASK;
    auto emitDir = [this](quint32 d) {
        if (d & B_UP) emit action(Up);
        else if (d & B_DOWN) emit action(Down);
        else if (d & B_LEFT) emit action(Left);
        else if (d & B_RIGHT) emit action(Right);
    };
    if (dir != m_heldDir) {
        m_heldDir = dir;
        m_repeating = false;
        m_repeat.restart();
        if (dir)
            emitDir(dir);
    } else if (dir) {
        const qint64 t = m_repeat.elapsed();
        if ((!m_repeating && t > 350) || (m_repeating && t > 80)) {
            m_repeating = true;
            m_repeat.restart();
            emitDir(dir);
        }
    }

    if (pressed & B_A) emit action(Accept);
    if (pressed & B_B) emit action(Back);
    if (pressed & B_LB) emit action(PrevCategory);
    if (pressed & B_RB) emit action(NextCategory);
    if (pressed & B_Y) emit action(Favorite);
    if (pressed & B_X) emit action(Menu);
    if (pressed & B_START) emit action(Options);
    if (pressed & B_BACK) emit action(Options);
    if (pressed & B_LT) emit action(PageUp);
    if (pressed & B_RT) emit action(PageDown);
}
