#pragma once

#include <QObject>
#include <QNetworkAccessManager>

class MappingFetcher : public QObject
{
    Q_OBJECT

public:
    explicit MappingFetcher(QObject *parent = nullptr);

    void start();

private slots:
    void handleMappingListFetched(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_Nam;
};
