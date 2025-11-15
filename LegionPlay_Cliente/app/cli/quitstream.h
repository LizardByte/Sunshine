#pragma once

#include <QObject>
#include <QVariant>

class ComputerManager;
class NvComputer;

namespace CliQuitStream
{

class LauncherPrivate;

class Launcher : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE_D(m_DPtr, Launcher)

public:
    explicit Launcher(QString computer, QObject *parent = nullptr);
    ~Launcher();

    Q_INVOKABLE void execute(ComputerManager *manager);
    Q_INVOKABLE bool isExecuted() const;

signals:
    void searchingComputer();
    void quittingApp();
    void failed(QString text);

private slots:
    void onComputerFound(NvComputer *computer);
    void onComputerSeekTimeout();
    void onQuitAppCompleted(QVariant error);

private:
    QScopedPointer<LauncherPrivate> m_DPtr;
};

}
