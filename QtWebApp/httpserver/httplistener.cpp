/**
  @file
  @author Stefan Frings
*/

#include "httplistener.h"
#include "httpconnectionhandler.h"
#include "httpconnectionhandlerpool.h"
#include <QCoreApplication>
#include <QNetworkProxy>

using namespace stefanfrings;

HttpListener::HttpListener(const QSettings* settings, HttpRequestHandler* requestHandler, QObject *parent)
    : QTcpServer(parent)
{
    Q_ASSERT(settings!=nullptr);
    Q_ASSERT(requestHandler!=nullptr);
    pool=nullptr;
    this->settings=settings;
    this->requestHandler=requestHandler;
    // Reqister type of socketDescriptor for signal/slot handling
    qRegisterMetaType<tSocketDescriptor>("tSocketDescriptor");
    // Start listening
    listen();
}


HttpListener::~HttpListener()
{
    close();
    qDebug("HttpListener: destroyed");
}


void HttpListener::listen()
{
    if (!pool)
    {
        pool=new HttpConnectionHandlerPool(settings,requestHandler);
    }
    QString host = settings->value("host").toString();
    quint16 port=settings->value("port").toUInt() & 0xFFFF;
    QTcpServer::listen(host.isEmpty() ? QHostAddress::Any : QHostAddress(host), port);
    setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    if (!isListening())
    {
        qCritical("HttpListener: Cannot bind on port %i: %s",port,qPrintable(errorString()));
    }
    else {
        qDebug("HttpListener: Listening on port %i",port);
    }
}


void HttpListener::close() {
    QTcpServer::close();
    qDebug("HttpListener: closed");
    if (pool) {
        delete pool;
        pool=nullptr;
    }
}

void stefanfrings::HttpListener::setHeadersHandler(const HeadersHandler& headersHandler)
{
  this->headersHandler = headersHandler;
  emit newHeadersHandler(headersHandler);
}

void HttpListener::incomingConnection(tSocketDescriptor socketDescriptor) {
#ifdef SUPERVERBOSE
    qDebug("HttpListener: New connection");
#endif

    HttpConnectionHandler* freeHandler=nullptr;

    if (pool)
    {
        bool needConnect = true;
        freeHandler=pool->getConnectionHandler(&needConnect);
        if (needConnect)
            connect(this, &HttpListener::newHeadersHandler, freeHandler, &HttpConnectionHandler::setHeadersHandler);
    }
    else
    {
        qCritical("Pool is not initialized.");
    }


    // Let the handler process the new connection.
    if (freeHandler)
    {
        freeHandler->setHeadersHandler(headersHandler);

        // The descriptor is passed via event queue because the handler lives in another thread
        QMetaObject::invokeMethod(freeHandler, "handleConnection", Qt::QueuedConnection, Q_ARG(tSocketDescriptor, socketDescriptor));
    }
    else
    {
        // Reject the connection
        qDebug("HttpListener: Too many incoming connections");
        QTcpSocket* socket=new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
        socket->write("HTTP/1.1 503 too many connections\r\nConnection: close\r\n\r\nToo many connections\r\n");
        socket->disconnectFromHost();
    }
}
