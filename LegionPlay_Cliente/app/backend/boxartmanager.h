#pragma once

#include "computermanager.h"
#include <QDir>
#include <QImage>
#include <QThreadPool>
#include <QRunnable>

class BoxArtManager : public QObject
{
    Q_OBJECT

    friend class NetworkBoxArtLoadTask;

public:
    explicit BoxArtManager(QObject *parent = nullptr);

    QUrl
    loadBoxArt(NvComputer* computer, NvApp& app);

    static
    void
    deleteBoxArt(NvComputer* computer);

signals:
    void
    boxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image);

public slots:

private slots:
    void
    handleBoxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image);

private:
    QUrl
    loadBoxArtFromNetwork(NvComputer* computer, int appId);

    QString
    getFilePathForBoxArt(NvComputer* computer, int appId);

    QDir m_BoxArtDir;
    QThreadPool m_ThreadPool;
};
