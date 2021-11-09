/**
  @file
  @author Stefan Frings
*/

#include "staticfilecontroller.h"
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <mutex>
#include "httpconnectionhandler.h"

using namespace stefanfrings;

StaticFileController::StaticFileController(const QSettings *settings, QObject* parent)
    :HttpRequestHandler(parent)
{
    maxAge=settings->value("maxAge","60000").toInt();
    encoding=settings->value("encoding","UTF-8").toString();
    docroot=settings->value("path",".").toString();
    if(!(docroot.startsWith(":/") || docroot.startsWith("qrc://")))
    {
        // Convert relative path to absolute, based on the directory of the config file.
        #ifdef Q_OS_WIN32
            if (QDir::isRelativePath(docroot) && settings->format()!=QSettings::NativeFormat)
        #else
            if (QDir::isRelativePath(docroot))
        #endif
        {
            QFileInfo configFile(settings->fileName());
            docroot=QFileInfo(configFile.absolutePath(),docroot).absoluteFilePath();
        }
    }
    qDebug("StaticFileController: docroot=%s, encoding=%s, maxAge=%i",qPrintable(docroot),qPrintable(encoding),maxAge);
    maxCachedFileSize=settings->value("maxCachedFileSize","65536").toInt();
    cache.setMaxCost(settings->value("cacheSize","1000000").toInt());
    cacheTimeout=settings->value("cacheTime","60000").toInt();
    qDebug("StaticFileController: cache timeout=%i, size=%i",cacheTimeout,cache.maxCost());
}


void StaticFileController::service(ServiceParams params)
{
    auto& request = *params.request;
    auto& response = *params.response;

    auto response_write = [&response](const QByteArray& data, bool lastPart = false) {
        response.getConnection().socketSafeExecution([&] { response.write(data, lastPart); });
    };

    QByteArray path = request.getPath();
    // Check if we have the file in cache
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    std::unique_lock lock{ mutex };

    CacheEntry* entry = cache.object(path);
    if (entry && (cacheTimeout == 0 || entry->created > now-cacheTimeout)) {
        QByteArray document = entry->document; //copy the cached document, because other threads may destroy the cached entry immediately after mutex unlock.
        QByteArray filename = entry->filename;
        lock.unlock();
        qDebug("StaticFileController: Cache hit for %s", path.constData());
        setContentType(filename,response);
        response.setHeader("Cache-Control","max-age=" + QByteArray::number(maxAge/1000));
        response_write(document);
    }
    else
    {
        lock.unlock();
        // The file is not in cache.
        qDebug("StaticFileController: Cache miss for %s", path.constData());
        // Forbid access to files outside the docroot directory
        if (path.contains("/..") || path.contains("/\\..") ||
                QFileInfo(docroot+path).filePath() != QFileInfo(docroot+path).absoluteFilePath())
        {
            qWarning("StaticFileController: detected forbidden characters in path %s",path.constData());
            response.setStatus(403,"forbidden");
            response_write("403 forbidden",true);
            return;
        }
        // If the filename is a directory, append index.html.
        if (QFileInfo(docroot+path).isDir())
            path += "/index.html";

        // Try to open the file
        QFile file(docroot + path);
        qDebug("StaticFileController: Open file %s", qPrintable(file.fileName()));
        if (file.open(QIODevice::ReadOnly)) {
            setContentType(path, response);
            response.setHeader("Cache-Control","max-age=" + QByteArray::number(maxAge/1000));

            CacheEntry* entryNew = (file.size() <= maxCachedFileSize) 
                ? new CacheEntry()
                : nullptr;

            // Return the file content and store it (if not very big) also into the cache
            while (!file.atEnd() && !file.error()) {
                QByteArray buffer = file.read(65536);
                response_write(buffer);
                if (entryNew)
                    entryNew->document.append(buffer);
            }
            if (entryNew) {
                entryNew->created = now;
                entryNew->filename = path;

                std::lock_guard guard(lock);
                cache.insert(request.getPath(), entryNew, entryNew->document.size());
            }
            file.close();
        }
        else {
            if (file.exists()) {
                qWarning("StaticFileController: Cannot open existing file %s for reading",qPrintable(file.fileName()));
                response.setStatus(403,"forbidden");
                response_write("403 forbidden", true);
            }
            else {
                response.setStatus(404,"not found");
                response_write("404 not found", true);
            }
        }
    }
}

void StaticFileController::setContentType(const QString& fileName, HttpResponse& response) const
{
    if (fileName.endsWith(".png"))
        response.setHeader("Content-Type", "image/png");
    else if (fileName.endsWith(".jpg"))
        response.setHeader("Content-Type", "image/jpeg");
    else if (fileName.endsWith(".gif"))
        response.setHeader("Content-Type", "image/gif");
    else if (fileName.endsWith(".pdf"))
        response.setHeader("Content-Type", "application/pdf");
    else if (fileName.endsWith(".txt"))
        response.setHeader("Content-Type", qPrintable("text/plain; charset="+encoding));
    else if (fileName.endsWith(".html") || fileName.endsWith(".htm"))
        response.setHeader("Content-Type", qPrintable("text/html; charset="+encoding));
    else if (fileName.endsWith(".css"))
        response.setHeader("Content-Type", "text/css");
    else if (fileName.endsWith(".js"))
        response.setHeader("Content-Type", "text/javascript");
    else if (fileName.endsWith(".svg"))
        response.setHeader("Content-Type", "image/svg+xml");
    else if (fileName.endsWith(".woff"))
        response.setHeader("Content-Type", "font/woff");
    else if (fileName.endsWith(".woff2"))
        response.setHeader("Content-Type", "font/woff2");
    else if (fileName.endsWith(".ttf"))
        response.setHeader("Content-Type", "application/x-font-ttf");
    else if (fileName.endsWith(".eot"))
        response.setHeader("Content-Type", "application/vnd.ms-fontobject");
    else if (fileName.endsWith(".otf"))
        response.setHeader("Content-Type", "application/font-otf");
    else if (fileName.endsWith(".json"))
        response.setHeader("Content-Type", "application/json");
    else if (fileName.endsWith(".xml"))
        response.setHeader("Content-Type", "text/xml");
    else if (fileName.endsWith(".exe"))
        response.setHeader("Content-Type", "application/exe");
    // Todo: add all of your content types
    else
        qDebug("StaticFileController: unknown MIME type for filename '%s'", qPrintable(fileName));
}
