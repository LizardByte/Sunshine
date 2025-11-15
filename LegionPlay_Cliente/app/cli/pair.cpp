#include "pair.h"

#include "backend/computermanager.h"
#include "backend/computerseeker.h"
#include <QCoreApplication>
#include <QTimer>

#define COMPUTER_SEEK_TIMEOUT 10000

namespace CliPair
{

enum State {
    StateInit,
    StateSeekComputer,
    StatePairing,
    StateFailure,
    StateComplete,
};

class Event
{
public:
    enum Type {
        ComputerFound,
        Executed,
        PairingCompleted,
        Timedout,
    };

    Event(Type type)
        : type(type), computerManager(nullptr), computer(nullptr) {}

    Type type;
    ComputerManager *computerManager;
    NvComputer *computer;
    QString errorMessage;
};

class LauncherPrivate
{
    Q_DECLARE_PUBLIC(Launcher)

public:
    LauncherPrivate(Launcher *q) : q_ptr(q) {}

    void handleEvent(Event event)
    {
        Q_Q(Launcher);

        switch (event.type) {
        // Occurs when CliPair becomes visible and the UI calls launcher's execute()
        case Event::Executed:
            if (m_State == StateInit) {
                m_State = StateSeekComputer;
                m_ComputerManager = event.computerManager;
                q->connect(m_ComputerManager, &ComputerManager::pairingCompleted,
                           q, &Launcher::onPairingCompleted);

                // If we weren't provided a predefined PIN, generate one now
                if (m_PredefinedPin.isEmpty()) {
                    m_PredefinedPin = m_ComputerManager->generatePinString();
                }

                m_ComputerSeeker = new ComputerSeeker(m_ComputerManager, m_ComputerName, q);
                q->connect(m_ComputerSeeker, &ComputerSeeker::computerFound,
                           q, &Launcher::onComputerFound);
                q->connect(m_ComputerSeeker, &ComputerSeeker::errorTimeout,
                           q, &Launcher::onTimeout);
                m_ComputerSeeker->start(COMPUTER_SEEK_TIMEOUT);

                emit q->searchingComputer();
            }
            break;
        // Occurs when searched computer is found
        case Event::ComputerFound:
            if (m_State == StateSeekComputer) {
                if (event.computer->pairState == NvComputer::PS_PAIRED) {
                    m_State = StateFailure;
                    QString msg = QObject::tr("%1 is already paired").arg(event.computer->name);
                    emit q->failed(msg);
                }
                else {
                    Q_ASSERT(!m_PredefinedPin.isEmpty());

                    m_State = StatePairing;
                    m_ComputerManager->pairHost(event.computer, m_PredefinedPin);
                    emit q->pairing(event.computer->name, m_PredefinedPin);
                }
            }
            break;
        // Occurs when pairing operation completes
        case Event::PairingCompleted:
            if (m_State == StatePairing) {
                if (event.errorMessage.isEmpty()) {
                    m_State = StateComplete;
                    emit q->success();
                }
                else {
                    m_State = StateFailure;
                    emit q->failed(event.errorMessage);
                }
            }
            break;
        // Occurs when computer search timed out
        case Event::Timedout:
            if (m_State == StateSeekComputer) {
                m_State = StateFailure;
                emit q->failed(QObject::tr("Failed to connect to %1").arg(m_ComputerName));
            }
            break;
        }
    }

    Launcher *q_ptr;
    QString m_ComputerName;
    QString m_PredefinedPin;
    ComputerManager *m_ComputerManager;
    ComputerSeeker *m_ComputerSeeker;
    NvComputer *m_Computer;
    State m_State;
    QTimer *m_TimeoutTimer;
};

Launcher::Launcher(QString computer, QString predefinedPin, QObject *parent)
    : QObject(parent),
      m_DPtr(new LauncherPrivate(this))
{
    Q_D(Launcher);
    d->m_ComputerName = computer;
    d->m_PredefinedPin = predefinedPin;
    d->m_State = StateInit;
    d->m_TimeoutTimer = new QTimer(this);
    d->m_TimeoutTimer->setSingleShot(true);
    connect(d->m_TimeoutTimer, &QTimer::timeout,
            this, &Launcher::onTimeout);
}

Launcher::~Launcher()
{
}

void Launcher::execute(ComputerManager *manager)
{
    Q_D(Launcher);
    Event event(Event::Executed);
    event.computerManager = manager;
    d->handleEvent(event);
}

bool Launcher::isExecuted() const
{
    Q_D(const Launcher);
    return d->m_State != StateInit;
}

void Launcher::onComputerFound(NvComputer *computer)
{
    Q_D(Launcher);
    Event event(Event::ComputerFound);
    event.computer = computer;
    d->handleEvent(event);
}

void Launcher::onTimeout()
{
    Q_D(Launcher);
    Event event(Event::Timedout);
    d->handleEvent(event);
}

void Launcher::onPairingCompleted(NvComputer* computer, QString error)
{
    Q_D(Launcher);
    Event event(Event::PairingCompleted);
    event.computer = computer;
    event.errorMessage = error;
    d->handleEvent(event);
}

}
