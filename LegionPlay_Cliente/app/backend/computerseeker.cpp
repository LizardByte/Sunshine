#include "computerseeker.h"
#include "computermanager.h"
#include <QTimer>

ComputerSeeker::ComputerSeeker(ComputerManager *manager, QString computerName, QObject *parent)
    : QObject(parent), m_ComputerManager(manager), m_ComputerName(computerName),
      m_TimeoutTimer(new QTimer(this))
{
    // If we know this computer, send a WOL packet to wake it up in case it is asleep.
    for (NvComputer * computer: m_ComputerManager->getComputers()) {
        if (this->matchComputer(computer)) {
            computer->wake();
        }
    }

    m_TimeoutTimer->setSingleShot(true);
    connect(m_TimeoutTimer, &QTimer::timeout,
            this, &ComputerSeeker::onTimeout);
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerSeeker::onComputerUpdated);
}

void ComputerSeeker::start(int timeout)
{
    m_TimeoutTimer->start(timeout);
    // Seek desired computer by both connecting to it directly (this may fail
    // if m_ComputerName is UUID, or the name that doesn't resolve to an IP
    // address) and by polling it using mDNS, hopefully one of these methods
    // would find the host
    m_ComputerManager->addNewHostManually(m_ComputerName);
    m_ComputerManager->startPolling();
}

void ComputerSeeker::onComputerUpdated(NvComputer *computer)
{
    if (!m_TimeoutTimer->isActive()) {
        return;
    }
    if (matchComputer(computer) && isOnline(computer)) {
        m_ComputerManager->stopPollingAsync();
        m_TimeoutTimer->stop();
        emit computerFound(computer);
    }
}

bool ComputerSeeker::matchComputer(NvComputer *computer) const
{
    QString value = m_ComputerName.toLower();

    if (computer->name.toLower() == value || computer->uuid.toLower() == value) {
        return true;
    }

    for (const NvAddress& addr : computer->uniqueAddresses()) {
        if (addr.address().toLower() == value || addr.toString().toLower() == value) {
            return true;
        }
    }

    return false;
}

bool ComputerSeeker::isOnline(NvComputer *computer) const
{
    return computer->state == NvComputer::CS_ONLINE;
}

void ComputerSeeker::onTimeout()
{
    m_TimeoutTimer->stop();
    m_ComputerManager->stopPollingAsync();
    emit errorTimeout();
}
