#include "mappingfetcher.h"
#include "path.h"

#include <QNetworkReply>

MappingFetcher::MappingFetcher(QObject *parent) :
    QObject(parent)
{
    m_Nam = new QNetworkAccessManager(this);

    // Never communicate over HTTP
    m_Nam->setStrictTransportSecurityEnabled(true);

    // Allow HTTP redirects
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    connect(m_Nam, &QNetworkAccessManager::finished,
            this, &MappingFetcher::handleMappingListFetched);
}

void MappingFetcher::start()
{
    if (!m_Nam) {
        Q_ASSERT(m_Nam);
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(5, 15, 1) && !defined(QT_NO_BEARERMANAGEMENT)
    // HACK: Set network accessibility to work around QTBUG-80947 (introduced in Qt 5.14.0 and fixed in Qt 5.15.1)
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    m_Nam->setNetworkAccessible(QNetworkAccessManager::Accessible);
    QT_WARNING_POP
#endif

    QUrl url("https://moonlight-stream.org/SDL_GameControllerDB/gamecontrollerdb.txt");
    QNetworkRequest request(url);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
#else
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);
#endif

    // Qt 5.12 introduced QNetworkRequest::IfModifiedSinceHeader. We _could_ implement it
    // by hand (including QDateTime conversion to the correct string form) for earlier Qt
    // versions, but that's a pain and this is just an optimization anyway. We'll do a bit
    // of extra work on those legacy Qt versions by fetching the GCDB each time we launch.
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    // Only download the file if it's newer than what we have
    QFileInfo existingFileInfo = Path::getCacheFileInfo("gamecontrollerdb.txt");
    if (existingFileInfo.exists()) {
        // Make sure the cached file looks reasonable. It should have some data and
        // the last modified time should not be in the future.
        QDateTime lastModifiedTime = existingFileInfo.lastModified().toUTC();
        if (existingFileInfo.size() > 0 && lastModifiedTime <= QDateTime::currentDateTimeUtc()) {
            request.setHeader(QNetworkRequest::IfModifiedSinceHeader, existingFileInfo.lastModified().toUTC());
        }
        else {
            Path::deleteCacheFile("gamecontrollerdb.txt");
        }
    }
#endif

    // We'll get a callback when this is finished
    m_Nam->get(request);
}

void MappingFetcher::handleMappingListFetched(QNetworkReply* reply)
{
    Q_ASSERT(reply->isFinished());

    // Delete the QNetworkAccessManager to free resources and
    // prevent the bearer plugin from polling in the background.
    m_Nam->deleteLater();
    m_Nam = nullptr;

    if (reply->error() == QNetworkReply::NoError) {
        // Queue the reply for deletion
        reply->deleteLater();

        // If we get a 304 back, Qt will happily just tell us our request was
        // successful and let us try to write our empty response to disk. Check
        // the status code directly to prevent this.
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute) == 304) {
            qInfo() << "Gamepad mappings are up to date";
        }
        else {
            // Update the cached data on disk for next call to applyMappings()
            QByteArray data = reply->readAll();
            if (!data.isEmpty()) {
                Path::writeCacheFile("gamecontrollerdb.txt", data);
            }

            qInfo() << "Downloaded updated gamepad mappings";
        }
    }
    else {
        qWarning() << "Failed to download updated gamepad mappings:" << reply->error();
        reply->deleteLater();
    }
}
