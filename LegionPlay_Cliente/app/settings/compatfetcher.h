#pragma once

#include <QObject>
#include <QNetworkAccessManager>

class CompatFetcher : public QObject
{
    Q_OBJECT

public:
    explicit CompatFetcher(QObject *parent = nullptr);

    void start();

    static bool isGfeVersionSupported(QString gfeVersion);

private slots:
    void handleCompatInfoFetched(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_Nam;
};
