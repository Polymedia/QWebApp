/**
  @file
  @author Stefan Frings
*/

#include "httpsessionstore.h"
#include <QDateTime>
#include <QUuid>
#include <mutex>

using namespace stefanfrings;

HttpSessionStore::HttpSessionStore(const QSettings *settings, QObject* parent)
    :QObject(parent)
{
    this->settings=settings;
    connect(&cleanupTimer,SIGNAL(timeout()),this,SLOT(sessionTimerEvent()));
    cleanupTimer.start(60000);
    cookieName=settings->value("cookieName","sessionid").toByteArray();
    expirationTime=settings->value("expirationTime",3600000).toInt();
    qDebug("HttpSessionStore: Sessions expire after %i milliseconds",expirationTime);
}

HttpSessionStore::~HttpSessionStore()
{
    cleanupTimer.stop();
}

QByteArray HttpSessionStore::getSessionId(const HttpRequest& request, HttpResponse& response)
{
    // The session ID in the response has priority because this one will be used in the next request.
    std::lock_guard{mutex};

    // Get the session ID from the response cookie
    QByteArray sessionId=response.getCookies().value(cookieName).getValue();
    if (sessionId.isEmpty())
    {
        // Get the session ID from the request cookie
        sessionId=request.getCookie(cookieName);
    }
    // Clear the session ID if there is no such session in the storage.
    if (!sessionId.isEmpty())
    {
        if (!sessions.contains(sessionId))
        {
            qDebug("HttpSessionStore: received invalid session cookie with ID %s",sessionId.constData());
            sessionId.clear();
        }
    }

    return sessionId;
}

HttpSession HttpSessionStore::getSession(const HttpRequest& request, HttpResponse& response, bool allowCreate)
{
    QByteArray sessionId=getSessionId(request,response);
    std::lock_guard{ mutex };

    if (!sessionId.isEmpty())
    {
        HttpSession session=sessions.value(sessionId);
        if (!session.isNull())
        {
            mutex.unlock();
            // Refresh the session cookie
            QByteArray cookieName=settings->value("cookieName","sessionid").toByteArray();
            QByteArray cookiePath=settings->value("cookiePath").toByteArray();
            QByteArray cookieComment=settings->value("cookieComment").toByteArray();
            QByteArray cookieDomain=settings->value("cookieDomain").toByteArray();
            response.setCookie(HttpCookie(cookieName,session.getId(),expirationTime/1000,cookiePath,cookieComment,cookieDomain));
            session.setLastAccess();
            return session;
        }
    }
    // Need to create a new session
    if (allowCreate)
    {
        QByteArray cookieName=settings->value("cookieName","sessionid").toByteArray();
        QByteArray cookiePath=settings->value("cookiePath").toByteArray();
        QByteArray cookieComment=settings->value("cookieComment").toByteArray();
        QByteArray cookieDomain=settings->value("cookieDomain").toByteArray();
        HttpSession session(true);
        qDebug("HttpSessionStore: create new session with ID %s",session.getId().constData());
        sessions.insert(session.getId(),session);
        response.setCookie(HttpCookie(cookieName,session.getId(),expirationTime/1000,cookiePath,cookieComment,cookieDomain));
        mutex.unlock();
        return session;
    }

    // Return a null session
    return HttpSession();
}

HttpSession HttpSessionStore::getSession(const QByteArray& id)
{
    HttpSession session;
    {
        std::lock_guard{ mutex };
        session = sessions.value(id);
    }
    session.setLastAccess();
    return session;
}

void HttpSessionStore::sessionTimerEvent()
{
    std::lock_guard{ mutex };

    qint64 now=QDateTime::currentMSecsSinceEpoch();
    QMap<QByteArray,HttpSession>::iterator i = sessions.begin();
    while (i != sessions.end())
    {
        QMap<QByteArray,HttpSession>::iterator prev = i;
        ++i;
        HttpSession session=prev.value();
        qint64 lastAccess=session.getLastAccess();
        if (now-lastAccess>expirationTime)
        {
            qDebug("HttpSessionStore: session %s expired",session.getId().constData());
            sessions.erase(prev);
        }
    }
}


/** Delete a session */
void HttpSessionStore::removeSession(HttpSession session)
{
    std::lock_guard{ mutex };
    sessions.remove(session.getId());
}
