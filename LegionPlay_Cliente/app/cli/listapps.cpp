#include "listapps.h"

#include "backend/boxartmanager.h"
#include "backend/computermanager.h"
#include "backend/computerseeker.h"

#include <QCoreApplication>

#define COMPUTER_SEEK_TIMEOUT 30000

namespace CliListApps
{

enum State {
    StateInit,
    StateSeekComputer,
    StateListApps,
    StateFailure,
};

class Event
{
public:
    enum Type {
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
        // Occurs when CLI main calls execute
        case Event::Executed:
            if (m_State == StateInit) {
                m_State = StateSeekComputer;
                m_ComputerManager = event.computerManager;

                m_ComputerSeeker = new ComputerSeeker(m_ComputerManager, m_ComputerName, q);
                q->connect(m_ComputerSeeker, &ComputerSeeker::computerFound,
                           q, &Launcher::onComputerFound);
                q->connect(m_ComputerSeeker, &ComputerSeeker::errorTimeout,
                           q, &Launcher::onComputerSeekTimeout);
                m_ComputerSeeker->start(COMPUTER_SEEK_TIMEOUT);

                m_BoxArtManager = new BoxArtManager(q);

                if (m_Arguments.isVerbose()) {
                    fprintf(stdout, "Establishing connection to PC...\n");
                }
            }
            break;
        // Occurs when computer search timed out
        case Event::ComputerSeekTimedout:
            if (m_State == StateSeekComputer) {
                fprintf(stderr, "%s\n", qPrintable(QString("Failed to connect to %1").arg(m_ComputerName)));

                QCoreApplication::exit(-1);
            }
            break;
        // Occurs when searched computer is found
        case Event::ComputerFound:
            if (m_State == StateSeekComputer) {
                if (event.computer->pairState == NvComputer::PS_PAIRED) {
                    m_State = StateListApps;
                    m_Computer = event.computer;

                    if (m_Arguments.isVerbose()) {
                        fprintf(stdout, "Loading app list...\n");
                    }

                    // To avoid race conditions where ComputerSeeker stops async polling, but
                    // ComputerManager is yet to update the app list, we will explicitly fetch the latest app list.
                    // Otherwise, it becomes complicated as we would have to guess whether ComputerManager
                    // would emit 1 signal (the list did not change) or 2 signals (indicating that the list has changed)
                    try {
                        NvHTTP http{m_Computer};

                        const auto appList = http.getAppList();
                        m_Arguments.isPrintCSV() ? printAppsCSV(appList) : printApps(appList);

                        QCoreApplication::exit(0);
                    } catch (std::exception& exception) {
                        fprintf(stderr, "%s\n", exception.what());
                        QCoreApplication::exit(1);
                    }
                } else {
                    m_State = StateFailure;
                    fprintf(stderr, "%s\n", qPrintable(QObject::tr("Computer %1 has not been paired. "
                                            "Please open Moonlight to pair before retrieving games list.")
                                            .arg(event.computer->name)));

                    QCoreApplication::exit(-1);
                }
            }
            break;
        }
    }

    void printApps(QVector<NvApp> apps) {
        for (int i = 0; i < apps.length(); i++) {
            fprintf(stdout, "%s\n", qPrintable(apps[i].name));
        }
    }

    void printAppsCSV(QVector<NvApp> apps) {
        fprintf(stdout, "Name, ID, HDR Support, App Collection Game, Hidden, Direct Launch, Boxart URL\n");
        for (int i = 0; i < apps.length(); i++) {
            printAppCSV(apps[i]);
        }
    }

    void printAppCSV(NvApp app) const
    {
        fprintf(stdout, "\"%s\",%d,%s,%s,%s,%s,\"%s\"\n", qPrintable(app.name),
                                                          app.id,
                                                          app.hdrSupported ? "true" : "false",
                                                          app.isAppCollectorGame ? "true" : "false",
                                                          app.hidden ? "true" : "false",
                                                          app.directLaunch ? "true" : "false",
                                                          qPrintable(m_BoxArtManager->loadBoxArt(m_Computer, app).toDisplayString()));
    }

    Launcher *q_ptr;
    ComputerManager *m_ComputerManager;
    QString m_ComputerName;
    ComputerSeeker *m_ComputerSeeker;
    BoxArtManager *m_BoxArtManager;
    NvComputer *m_Computer;
    State m_State;
    ListCommandLineParser m_Arguments;
};

Launcher::Launcher(QString computer, ListCommandLineParser arguments, QObject *parent)
    : QObject(parent),
      m_DPtr(new LauncherPrivate(this))
{
    Q_D(Launcher);
    d->m_ComputerName = computer;
    d->m_State = StateInit;
    d->m_Arguments = arguments;
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

}
