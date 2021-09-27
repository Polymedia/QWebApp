/**
  @file
  @author Stefan Frings
*/

#include "httpconnectionhandler.h"
#include "httpresponse.h"

// #ifndef WIN32_LEAN_AND_MEAN
// #define WIN32_LEAN_AND_MEAN
// #endif
// #include <winsock2.h>
// #include <Ws2tcpip.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

using namespace stefanfrings;

HttpConnectionHandler::HttpConnectionHandler(const QSettings *settings, HttpRequestHandler *requestHandler, const QSslConfiguration* sslConfiguration)
    : QObject()
{
    Q_ASSERT(settings!=nullptr);
    Q_ASSERT(requestHandler!=nullptr);
    this->settings=settings;
    this->requestHandler=requestHandler;
    this->sslConfiguration=sslConfiguration;
    currentRequest=nullptr;
    busy=false;

    // execute signals in a new thread
    thread = new QThread();
    thread->start();
    qDebug("HttpConnectionHandler (%p): thread started", static_cast<void*>(this));
    moveToThread(thread);
    readTimer.moveToThread(thread);
    readTimer.setSingleShot(true);

    // Create TCP or SSL socket
    createSocket();
    socket->moveToThread(thread);

    // Connect signals
    connect(socket, SIGNAL(readyRead()), SLOT(read()));
    connect(socket, SIGNAL(disconnected()), SLOT(disconnected()));

    connect(socket, SIGNAL(errorOccurred), SLOT(errorOccurred(QAbstractSocket::SocketError)));
    connect(socket, SIGNAL(stateChanged), SLOT(stateChanged(QAbstractSocket::SocketState)));
   
    connect(&readTimer, SIGNAL(timeout()), SLOT(readTimeout()));
    connect(thread, SIGNAL(finished()), this, SLOT(thread_done()));

    qDebug("HttpConnectionHandler (%p): constructed", static_cast<void*>(this));
}

void HttpConnectionHandler::stateChanged(QAbstractSocket::SocketState socketState)
{
    qDebug("HttpConnectionHandler (%p): stateChanged", static_cast<void*>(this));
}

void HttpConnectionHandler::errorOccurred(QAbstractSocket::SocketError socketError)
{
    qDebug("HttpConnectionHandler (%p): errorOccurred", static_cast<void*>(this));
}

void HttpConnectionHandler::thread_done()
{
    readTimer.stop();
    socket->close();
    delete socket;
    qDebug("HttpConnectionHandler (%p): thread stopped", static_cast<void*>(this));
}


HttpConnectionHandler::~HttpConnectionHandler()
{
    thread->quit();
    thread->wait();
    thread->deleteLater();
    qDebug("HttpConnectionHandler (%p): destroyed", static_cast<void*>(this));
}


void HttpConnectionHandler::createSocket()
{
    // If SSL is supported and configured, then create an instance of QSslSocket
    #ifndef QT_NO_SSL
        if (sslConfiguration)
        {
            QSslSocket* sslSocket=new QSslSocket();
            sslSocket->setSslConfiguration(*sslConfiguration);
            socket=sslSocket;
            qDebug("HttpConnectionHandler (%p): SSL is enabled", static_cast<void*>(this));
            return;
        }
    #endif
    // else create an instance of QTcpSocket
    socket=new QTcpSocket();

    int enableKeepAlive = 1;
    auto fd = socket->socketDescriptor();
    auto res = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&enableKeepAlive, sizeof(enableKeepAlive));
    if (res != 0)
        qInfo("HttpConnectionHandler (%p): SO_KEEPALIVE err %d", static_cast<void*>(this), res);

    int maxIdle = 10; /* seconds */
    res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (char*)&maxIdle, sizeof(maxIdle));
    if (res != 0)
        qInfo("HttpConnectionHandler (%p): TCP_KEEPIDLE err %d", static_cast<void*>(this), res);

    int count = 3;  // send up to 3 keepalive packets out, then disconnect if no response
    res = setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (char*)&count, sizeof(count));
    if (res != 0)
        qInfo("HttpConnectionHandler (%p): TCP_KEEPCNT err %d", static_cast<void*>(this), res);

    int interval = 2;   // send a keepalive packet out every 2 seconds (after the 5 second idle period)
    res = setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (char*)&interval, sizeof(interval));
    if (res != 0)
        qInfo("HttpConnectionHandler (%p): TCP_KEEPINTVL err %d", static_cast<void*>(this), res);
}


void HttpConnectionHandler::handleConnection(const tSocketDescriptor& socketDescriptor)
{
    qDebug("HttpConnectionHandler (%p): handle new connection", static_cast<void*>(this));
    busy = true;
    Q_ASSERT(socket->isOpen()==false); // if not, then the handler is already busy

    //UGLY workaround - we need to clear writebuffer before reusing this socket
    //https://bugreports.qt-project.org/browse/QTBUG-28914
    socket->connectToHost("",0);
    socket->abort();

    if (!socket->setSocketDescriptor(socketDescriptor))
    {
        qCritical("HttpConnectionHandler (%p): cannot initialize socket: %s",
                  static_cast<void*>(this),qPrintable(socket->errorString()));
        return;
    }

    #ifndef QT_NO_SSL
        // Switch on encryption, if SSL is configured
        if (sslConfiguration)
        {
            qDebug("HttpConnectionHandler (%p): Starting encryption", static_cast<void*>(this));
            (static_cast<QSslSocket*>(socket))->startServerEncryption();
        }
    #endif

    // Start timer for read timeout
    int readTimeout=settings->value("readTimeout",10000).toInt();
    readTimer.start(readTimeout);
    // delete previous request
    delete currentRequest;
    currentRequest=nullptr;
}


bool HttpConnectionHandler::isBusy() const
{
    return busy;
}

void HttpConnectionHandler::setBusy()
{
    this->busy = true;
}

void stefanfrings::HttpConnectionHandler::setHeadersHandler(const HeadersHandler& headersHandler)
{
    this->headersHandler = headersHandler;

    emit newHeadersHandler(headersHandler);
}

void HttpConnectionHandler::readTimeout()
{
    qDebug("HttpConnectionHandler (%p): read timeout occured",static_cast<void*>(this));

    //Commented out because QWebView cannot handle this.
    //socket->write("HTTP/1.1 408 request timeout\r\nConnection: close\r\n\r\n408 request timeout\r\n");

    while(socket->bytesToWrite()) socket->waitForBytesWritten();
    socket->disconnectFromHost();
    delete currentRequest;
    currentRequest=nullptr;
}

void HttpConnectionHandler::disconnected()
{
    qDebug("HttpConnectionHandler (%p): disconnected", static_cast<void*>(this));
    socket->close();
    readTimer.stop();
    busy = false;
}

void HttpConnectionHandler::read()
{
    // The loop adds support for HTTP pipelinig
    while (socket->bytesAvailable())
    {
        #ifdef SUPERVERBOSE
            qDebug("HttpConnectionHandler (%p): read input",static_cast<void*>(this));
        #endif

        // Create new HttpRequest object if necessary
        if (!currentRequest)
        {
            currentRequest=new HttpRequest(settings, headersHandler);

            connect(this, &HttpConnectionHandler::newHeadersHandler, currentRequest, &HttpRequest::setHeadersHandler);
        }

        // Collect data for the request object
        while (socket->bytesAvailable() &&
               currentRequest->getStatus() != HttpRequest::complete &&
               currentRequest->getStatus() != HttpRequest::abort &&
               currentRequest->getStatus() != HttpRequest::wrongHeaders)
        {
            currentRequest->readFromSocket(socket);
            if (currentRequest->getStatus()==HttpRequest::waitForBody)
            {
                // Restart timer for read timeout, otherwise it would
                // expire during large file uploads.
                int readTimeout=settings->value("readTimeout",10000).toInt();
                readTimer.start(readTimeout);
            }
        }

        // If some headers fails checking, return status code and error text from handler
        if (currentRequest && currentRequest->getStatus() == HttpRequest::wrongHeaders) {
          const auto [statusCode, text] = currentRequest->getHttpError();
          const QString response = QString{"HTTP/1.1 %1\r\nConnection: "
                                           "close\r\n\r\n%2\r\n"}
                                       .arg(statusCode)
                                       .arg(text);

          socket->write(response.toUtf8().constData());
          while (socket->bytesToWrite())
              socket->waitForBytesWritten();
          socket->disconnectFromHost();
          delete currentRequest;
          currentRequest = nullptr;
          return;
        }

        // If the request is aborted, return error message and close the connection
        if (currentRequest && currentRequest->getStatus()==HttpRequest::abort)
        {
            socket->write("HTTP/1.1 413 entity too large\r\nConnection: close\r\n\r\n413 Entity too large\r\n");
            while(socket->bytesToWrite()) socket->waitForBytesWritten();
            socket->disconnectFromHost();
            delete currentRequest;
            currentRequest=nullptr;
            return;
        }

        // If the request is complete, let the request mapper dispatch it
        if (currentRequest && currentRequest->getStatus()==HttpRequest::complete)
        {
            readTimer.stop();
            qDebug("HttpConnectionHandler (%p): received request",static_cast<void*>(this));

            // Copy the Connection:close header to the response
            HttpResponse response(socket);
            bool closeConnection=QString::compare(currentRequest->getHeader("Connection"),"close",Qt::CaseInsensitive)==0;
            if (closeConnection)
            {
                response.setHeader("Connection","close");
            }

            // In case of HTTP 1.0 protocol add the Connection:close header.
            // This ensures that the HttpResponse does not activate chunked mode, which is not spported by HTTP 1.0.
            else
            {
                bool http1_0=QString::compare(currentRequest->getVersion(),"HTTP/1.0",Qt::CaseInsensitive)==0;
                if (http1_0)
                {
                    closeConnection=true;
                    response.setHeader("Connection","close");
                }
            }

            // Call the request mapper
            try
            {
                requestHandler->service(*currentRequest, response);
            }
            catch (...)
            {
                qCritical("HttpConnectionHandler (%p): An uncatched exception occured in the request handler",
                          static_cast<void*>(this));
            }

            // Finalize sending the response if not already done
            if (!response.hasSentLastPart())
            {
                response.write(QByteArray(),true);
            }

            qDebug("HttpConnectionHandler (%p): finished request",static_cast<void*>(this));

            // Find out whether the connection must be closed
            if (!closeConnection)
            {
                // Maybe the request handler or mapper added a Connection:close header in the meantime
                bool closeResponse=QString::compare(response.getHeaders().value("Connection"),"close",Qt::CaseInsensitive)==0;
                if (closeResponse==true)
                {
                    closeConnection=true;
                }
                else
                {
                    // If we have no Content-Length header and did not use chunked mode, then we have to close the
                    // connection to tell the HTTP client that the end of the response has been reached.
                    bool hasContentLength=response.getHeaders().contains("Content-Length");
                    if (!hasContentLength)
                    {
                        bool hasChunkedMode=QString::compare(response.getHeaders().value("Transfer-Encoding"),"chunked",Qt::CaseInsensitive)==0;
                        if (!hasChunkedMode)
                        {
                            closeConnection=true;
                        }
                    }
                }
            }

            // Close the connection or prepare for the next request on the same connection.
            if (closeConnection)
            {
                while(socket->bytesToWrite()) socket->waitForBytesWritten();
                socket->disconnectFromHost();
            }
            else
            {
                // Start timer for next request
                int readTimeout=settings->value("readTimeout",10000).toInt();
                readTimer.start(readTimeout);
            }
            delete currentRequest;
            currentRequest=nullptr;
        }
    }
}
