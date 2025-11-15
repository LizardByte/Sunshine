#include "quitstream.h"

#include "backend/computermanager.h"
#include "backend/computerseeker.h"
#include "streaming/session.h"

#include <QCoreApplication>
#include <QTimer>

#define COMPUTER_SEEK_TIMEOUT 10000

namespace CliQuitStream
{

enum State {
    StateInit,
    StateSeekComputer,
    StateQuitApp,
    StateFailure,
};

class Event
{
public:
    enum Type {
        AppQuitCompleted,
        ComputerFound,
        ComputerSeekTimedout,
        Executed,
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
        NvApp app;

        switch (event.type) {
        // Occurs when CliQuitStreamSegue becomes visible and the UI calls launcher's execute()
        case Event::Executed:
            if (m_State == StateInit) {
                m_State = StateSeekComputer;
                m_ComputerManager = event.computerManager;
                q->connect(m_ComputerManager, &ComputerManager::quitAppCompleted,
                           q, &Launcher::onQuitAppCompleted);

                m_ComputerSeeker = new ComputerSeeker(m_ComputerManager, m_ComputerName, q);
                q->connect(m_ComputerSeeker, &ComputerSeeker::computerFound,
                           q, &Launcher::onComputerFound);
                q->connect(m_ComputerSeeker, &ComputerSeeker::errorTimeout,
                           q, &Launcher::onComputerSeekTimeout);
                m_ComputerSeeker->start(COMPUTER_SEEK_TIMEOUT);

                emit q->searchingComputer();
            }
            break;
        // Occurs when computer search timed out
        case Event::ComputerSeekTimedout:
            if (m_State == StateSeekComputer) {
                m_State = StateFailure;
                emit q->failed(QObject::tr("Failed to connect to %1").arg(m_ComputerName));
            }
            break;
        // Occurs when searched computer is found
        case Event::ComputerFound:
            if (m_State == StateSeekComputer) {
                if (event.computer->pairState == NvComputer::PS_PAIRED) {
                    m_State = StateQuitApp;
                    emit q->quittingApp();
                    m_ComputerManager->quitRunningApp(event.computer);
                } else {
                    m_State = StateFailure;
                    QString msg = QObject::tr("Computer %1 has not been paired. "
                                              "Please open Moonlight to pair before streaming.")
                            .arg(event.computer->name);
                    emit q->failed(msg);
                }
            }
            break;
        // Occurs when app quit completed (error message is set if failed)
        case Event::AppQuitCompleted:
            if (m_State == StateQuitApp) {
                if (event.errorMessage.isEmpty()) {
                    QCoreApplication::exit(0);
                } else {
                    m_State = StateFailure;
                    emit q->failed(QObject::tr("Quitting app failed, reason: %1").arg(event.errorMessage));
                }
            }
            break;
        }
    }

    Launcher *q_ptr;
    ComputerManager *m_ComputerManager;
    QString m_ComputerName;
    ComputerSeeker *m_ComputerSeeker;
    State m_State;
    QTimer *m_TimeoutTimer;
};

Launcher::Launcher(QString computer, QObject *parent)
    : QObject(parent),
      m_DPtr(new LauncherPrivate(this))
{
    Q_D(Launcher);
    d->m_ComputerName = computer;
    d->m_State = StateInit;
    d->m_TimeoutTimer = new QTimer(this);
    d->m_TimeoutTimer->setSingleShot(true);
    connect(d->m_TimeoutTimer, &QTimer::timeout,
            this, &Launcher::onComputerSeekTimeout);
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

void Launcher::onComputerSeekTimeout()
{
    Q_D(Launcher);
    Event event(Event::ComputerSeekTimedout);
    d->handleEvent(event);
}

void Launcher::onQuitAppCompleted(QVariant error)
{
    Q_D(Launcher);
    Event event(Event::AppQuitCompleted);
    event.errorMessage = error.toString();
    d->handleEvent(event);
}

}
