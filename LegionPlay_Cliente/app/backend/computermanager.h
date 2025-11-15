#pragma once

#include "nvcomputer.h"
#include "settings/streamingpreferences.h"
#include "settings/compatfetcher.h"

#include <qmdnsengine/server.h>
#include <qmdnsengine/cache.h>
#include <qmdnsengine/browser.h>
#include <qmdnsengine/service.h>
#include <qmdnsengine/resolver.h>

#include <QThread>
#include <QReadWriteLock>
#include <QSettings>
#include <QRunnable>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>

class ComputerManager;

class DelayedFlushThread : public QThread
{
    Q_OBJECT

public:
    DelayedFlushThread(ComputerManager* cm)
        : m_ComputerManager(cm)
    {
        setObjectName("CM Delayed Flush Thread");
    }

    void run();

private:
    ComputerManager* m_ComputerManager;
};

class MdnsPendingComputer : public QObject
{
    Q_OBJECT

public:
    explicit MdnsPendingComputer(const QSharedPointer<QMdnsEngine::Server> server,
                                 const QMdnsEngine::Service& service)
        : m_Hostname(service.hostname()),
          m_Port(service.port()),
          m_ServerWeak(server),
          m_Resolver(nullptr)
    {
        // Start resolving
        resolve();
    }

    virtual ~MdnsPendingComputer()
    {
        delete m_Resolver;
    }

    QString hostname()
    {
        return m_Hostname;
    }

    uint16_t port()
    {
        return m_Port;
    }

private slots:
    void handleResolvedTimeout()
    {
        if (m_Addresses.isEmpty()) {
            if (m_Retries-- > 0) {
                // Try again
                qInfo() << "Resolving" << hostname() << "timed out. Retrying...";
                resolve();
            }
            else {
                qWarning() << "Giving up on resolving" << hostname() << "after repeated failures";
                cleanup();
            }
        }
        else {
            Q_ASSERT(!m_Addresses.isEmpty());
            emit resolvedHost(this, m_Addresses);
        }
    }

    void handleResolvedAddress(const QHostAddress& address)
    {
        qInfo() << "Resolved" << hostname() << "to" << address;
        m_Addresses.push_back(address);
    }

signals:
    void resolvedHost(MdnsPendingComputer*,QVector<QHostAddress>&);

private:
    void cleanup()
    {
        // Delete our resolver, so we're guaranteed that nothing is referencing m_Server.
        delete m_Resolver;
        m_Resolver = nullptr;

        // Now delete our strong reference that we held on behalf of m_Resolver.
        // The server may be destroyed after we make this call.
        m_Server.reset();
    }

    void resolve()
    {
        // Clean up any existing resolver object and server references
        cleanup();

        // Re-acquire a strong reference if the server still exists.
        m_Server = m_ServerWeak.toStrongRef();
        if (!m_Server) {
            return;
        }

        m_Resolver = new QMdnsEngine::Resolver(m_Server.data(), m_Hostname);
        connect(m_Resolver, &QMdnsEngine::Resolver::resolved,
                this, &MdnsPendingComputer::handleResolvedAddress);
        QTimer::singleShot(2000, this, &MdnsPendingComputer::handleResolvedTimeout);
    }

    QByteArray m_Hostname;
    uint16_t m_Port;
    QWeakPointer<QMdnsEngine::Server> m_ServerWeak;
    QSharedPointer<QMdnsEngine::Server> m_Server;
    QMdnsEngine::Resolver* m_Resolver;
    QVector<QHostAddress> m_Addresses;
    int m_Retries = 10;
};

class ComputerPollingEntry
{
public:
    ComputerPollingEntry()
        : m_ActiveThread(nullptr)
    {

    }

    virtual ~ComputerPollingEntry()
    {
        interrupt();

        // interrupt() should have taken care of this
        Q_ASSERT(m_ActiveThread == nullptr);

        for (QThread* thread : m_InactiveList) {
            thread->wait();
            delete thread;
        }
    }

    bool isActive()
    {
        cleanInactiveList();

        return m_ActiveThread != nullptr;
    }

    void setActiveThread(QThread* thread)
    {
        cleanInactiveList();

        Q_ASSERT(!isActive());
        m_ActiveThread = thread;
    }

    void interrupt()
    {
        cleanInactiveList();

        if (m_ActiveThread != nullptr) {
            // Interrupt the active thread
            m_ActiveThread->requestInterruption();

            // Place it on the inactive list awaiting death
            m_InactiveList.append(m_ActiveThread);

            m_ActiveThread = nullptr;
        }
    }

private:
    void cleanInactiveList()
    {
        QMutableListIterator<QThread*> i(m_InactiveList);

        // Reap any threads that have finished
        while (i.hasNext()) {
            i.next();

            QThread* thread = i.value();
            if (thread->isFinished()) {
                delete thread;
                i.remove();
            }
        }
    }

    QThread* m_ActiveThread;
    QList<QThread*> m_InactiveList;
};

class ComputerManager : public QObject
{
    Q_OBJECT

    friend class DeferredHostDeletionTask;
    friend class PendingAddTask;
    friend class PendingPairingTask;
    friend class DelayedFlushThread;

public:
    explicit ComputerManager(StreamingPreferences* prefs);

    virtual ~ComputerManager();

    Q_INVOKABLE void startPolling();

    Q_INVOKABLE void stopPollingAsync();

    Q_INVOKABLE void addNewHostManually(QString address);

    void addNewHost(NvAddress address, bool mdns, NvAddress mdnsIpv6Address = NvAddress());

    QString generatePinString();

    void pairHost(NvComputer* computer, QString pin);

    void quitRunningApp(NvComputer* computer);

    QVector<NvComputer*> getComputers();

    // computer is deleted inside this call
    void deleteHost(NvComputer* computer);

    void renameHost(NvComputer* computer, QString name);

    void clientSideAttributeUpdated(NvComputer* computer);

signals:
    void computerStateChanged(NvComputer* computer);

    void pairingCompleted(NvComputer* computer, QString error);

    void computerAddCompleted(QVariant success, QVariant detectedPortBlocking);

    void quitAppCompleted(QVariant error);

private slots:
    void handleAboutToQuit();

    void handleComputerStateChanged(NvComputer* computer);

    void handleMdnsServiceResolved(MdnsPendingComputer* computer, QVector<QHostAddress>& addresses);

private:
    void saveHosts();

    void saveHost(NvComputer* computer);

    QHostAddress getBestGlobalAddressV6(QVector<QHostAddress>& addresses);

    void startPollingComputer(NvComputer* computer);

    StreamingPreferences* m_Prefs;
    int m_PollingRef;
    QReadWriteLock m_Lock;
    QMap<QString, NvComputer*> m_KnownHosts;
    QMap<QString, ComputerPollingEntry*> m_PollEntries;
    QHash<QString, NvComputer> m_LastSerializedHosts; // Protected by m_DelayedFlushMutex
    QSharedPointer<QMdnsEngine::Server> m_MdnsServer;
    QMdnsEngine::Browser* m_MdnsBrowser;
    QVector<MdnsPendingComputer*> m_PendingResolution;
    CompatFetcher m_CompatFetcher;
    DelayedFlushThread* m_DelayedFlushThread;
    QMutex m_DelayedFlushMutex; // Lock ordering: Must never be acquired while holding NvComputer lock
    QWaitCondition m_DelayedFlushCondition;
    bool m_NeedsDelayedFlush;
};
