#pragma once

#include <QObject>

class ComputerManager;
class NvComputer;
class QTimer;

class ComputerSeeker : public QObject
{
    Q_OBJECT
public:
    explicit ComputerSeeker(ComputerManager *manager, QString computerName, QObject *parent = nullptr);

    void start(int timeout);

signals:
    void computerFound(NvComputer *computer);
    void errorTimeout();

private slots:
    void onComputerUpdated(NvComputer *computer);
    void onTimeout();

private:
    bool matchComputer(NvComputer *computer) const;
    bool isOnline(NvComputer *computer) const;

private:
    ComputerManager *m_ComputerManager;
    QString m_ComputerName;
    QTimer *m_TimeoutTimer;
};
