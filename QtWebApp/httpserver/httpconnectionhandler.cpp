/**
  @file
  @author Stefan Frings
*/

#include "httpconnectionhandler.h"
#include "httpresponse.h"

using namespace stefanfrings;

HttpConnectionHandler::HttpConnectionHandler(const QSettings *settings, HttpRequestHandler *requestHandler, const QSslConfiguration* sslConfiguration)
    : QObject()
{
    Q_ASSERT(settings!=nullptr);
    Q_ASSERT(requestHandler!=nullptr);
    this->settings=settings;
    this->requestHandler=requestHandler;
    this->sslConfiguration=sslConfiguration;
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
    connect(socket, SIGNAL(disconnected()), SLOT(disconnected())/*, Qt::QueuedConnection*/);
    connect(&readTimer, SIGNAL(timeout()), SLOT(readTimeout()));
    connect(thread, SIGNAL(finished()), this, SLOT(thread_done()));

//     connect(this, &HttpConnectionHandler::disconnectFromHostSignal, &HttpConnectionHandler::disconnectFromHost );
//     connect(this, &HttpConnectionHandler::sendLastPartSignal, &HttpConnectionHandler::sendLastPart );
//     connect(this, &HttpConnectionHandler::finalizeReadSignal, &HttpConnectionHandler::finalizeRead);


    auto b = connect(this, &HttpConnectionHandler::disconnectFromHostSignal, this, &HttpConnectionHandler::disconnectFromHost, Qt::QueuedConnection);
    qDebug() << b;
     b = connect(this, &HttpConnectionHandler::sendLastPartSignal, this, &HttpConnectionHandler::sendLastPart, Qt::QueuedConnection);
     qDebug() << b;
     b = connect(this, &HttpConnectionHandler::finalizeReadSignal, this, &HttpConnectionHandler::finalizeRead, Qt::QueuedConnection);
     qDebug() << b;

//     connect(this, SIGNAL(disconnectFromHostSignal()), SLOT(disconnectFromHost()), Qt::QueuedConnection);
//     connect(this, SIGNAL(sendLastPartSignal(LastPartSignalParam)), SLOT(sendLastPart(LastPartSignalParam)), Qt::QueuedConnection);
//    connect(this, SIGNAL(finalizeReadSignal()), SLOT(finalizeRead()), Qt::QueuedConnection);


    qDebug("HttpConnectionHandler (%p): constructed", static_cast<void*>(this));
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
    waitForReadThread();
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

    disconnectFromHost();
}

void HttpConnectionHandler::disconnected()
{
    qDebug("HttpConnectionHandler (%p): disconnected", static_cast<void*>(this));

    std::lock_guard lock{ m_disconnectionMutex };
    if (!m_threadReadSocket.joinable())
        freeUnsafe();
    else {
        std::lock_guard lock{ m_cancelerMutex };
        if (m_canceller)
            m_canceller->cancel();
    }
}

void stefanfrings::HttpConnectionHandler::waitForReadThread()
{
    if (m_threadReadSocket.joinable())
        m_threadReadSocket.join();
}

void stefanfrings::HttpConnectionHandler::disconnectFromHost()
{
    while (socket->bytesToWrite())
        socket->waitForBytesWritten();
    socket->disconnectFromHost();
}

void HttpConnectionHandler::sendLastPart(LastPartSignalParam param)
{
    auto [ptrResponse, closeConnection] = param;
    auto& response = *ptrResponse;

    // Finalize sending the response if not already done
    if (!response.hasSentLastPart())
        response.write(QByteArray(), true);

    qDebug("HttpConnectionHandler (%p): finished request", static_cast<void*>(this));

    // Find out whether the connection must be closed
    if (!closeConnection)
    {
        // Maybe the request handler or mapper added a Connection:close header in the meantime
        if (0 == QString::compare(response.getHeaders().value("Connection"), "close", Qt::CaseInsensitive))
            closeConnection = true;
        else
            // If we have no Content-Length header and did not use chunked mode, then we have to close the
            // connection to tell the HTTP client that the end of the response has been reached.
            if (!response.getHeaders().contains("Content-Length")
                && 0 != QString::compare(response.getHeaders().value("Transfer-Encoding"), "chunked", Qt::CaseInsensitive))
            {
                closeConnection = true;
            }
    }

    // Close the connection or prepare for the next request on the same connection.
    if (closeConnection)
        disconnectFromHost();
    else
    {
        // Start timer for next request
        const int readTimeout = settings->value("readTimeout", 10000).toInt();
        readTimer.start(readTimeout);
    }
}

void stefanfrings::HttpConnectionHandler::freeUnsafe()
{
    socket->close();
    readTimer.stop();
    busy = false;
}

void HttpConnectionHandler::read()
{
    std::lock_guard lock{ m_disconnectionMutex };
    waitForReadThread();

    m_threadReadSocket = std::thread ([this] {
        HttpRequest currentRequest(settings, headersHandler);
        connect(this, &HttpConnectionHandler::newHeadersHandler, &currentRequest, &HttpRequest::setHeadersHandler);

//         connect(this, &HttpConnectionHandler::disconnectFromHostSignal, &HttpConnectionHandler::disconnectFromHost);
//         connect(this, &HttpConnectionHandler::sendLastPartSignal, &HttpConnectionHandler::sendLastPart);
//         connect(this, &HttpConnectionHandler::finalizeReadSignal, &HttpConnectionHandler::finalizeRead);


        auto disconnectFromHostLocal = [this]
        {
            emit  disconnectFromHostSignal();
        };

        // The loop adds support for HTTP pipelinig
        while (socket->bytesAvailable())
        {
#ifdef SUPERVERBOSE
            qDebug("HttpConnectionHandler (%p): read input", static_cast<void*>(this));
#endif

            // Collect data for the request object
            while (socket->bytesAvailable() &&
                currentRequest.getStatus() != HttpRequest::complete &&
                currentRequest.getStatus() != HttpRequest::abort &&
                currentRequest.getStatus() != HttpRequest::wrongHeaders)
            {
                currentRequest.readFromSocket(socket);
                if (currentRequest.getStatus() == HttpRequest::waitForBody)
                {
                    // Restart timer for read timeout, otherwise it would
                    // expire during large file uploads.
                    int readTimeout = settings->value("readTimeout", 10000).toInt();
                    readTimer.start(readTimeout);
                }
            }

            // If some headers fails checking, return status code and error text from handler
            if (currentRequest.getStatus() == HttpRequest::wrongHeaders) {
                const auto [statusCode, text] = currentRequest.getHttpError();
                const QString response = QString{ "HTTP/1.1 %1\r\nConnection: "
                                                 "close\r\n\r\n%2\r\n" }
                    .arg(statusCode)
                    .arg(text);

                socket->write(response.toUtf8().constData());
                disconnectFromHostLocal();
                return;
            }

            // If the request is aborted, return error message and close the connection
            if (currentRequest.getStatus() == HttpRequest::abort)
            {
                socket->write("HTTP/1.1 413 entity too large\r\nConnection: close\r\n\r\n413 Entity too large\r\n");
                disconnectFromHostLocal();
                return;
            }

            // If the request is complete, let the request mapper dispatch it
            if (currentRequest.getStatus() == HttpRequest::complete)
            {
                readTimer.stop();
                qDebug("HttpConnectionHandler (%p): received request", static_cast<void*>(this));

                // Copy the Connection:close header to the response
                auto ptrResponse = std::make_shared<HttpResponse>(socket);
                auto& response = *ptrResponse;

                bool closeConnection = QString::compare(currentRequest.getHeader("Connection"), "close", Qt::CaseInsensitive) == 0
                    // In case of HTTP 1.0 protocol add the Connection:close header.
                    // This ensures that the HttpResponse does not activate chunked mode, which is not spported by HTTP 1.0.
                    || QString::compare(currentRequest.getVersion(), "HTTP/1.0", Qt::CaseInsensitive) == 0;
                if (closeConnection)
                    response.setHeader("Connection", "close");

                // Call the request mapper

                try
                {
                    requestHandler->service(currentRequest, response, [this](CancellerRef ref) { 
                        std::lock_guard lock{ m_cancelerMutex };
                        m_canceller = ref;
                    });
                }
                catch (...)
                {
                    qCritical("HttpConnectionHandler (%p): An uncatched exception occured in the request handler",
                        static_cast<void*>(this));
                }

                emit sendLastPartSignal(LastPartSignalParam{ ptrResponse, closeConnection });
            }
        }

        emit finalizeReadSignal();

//         std::lock_guard lock{ m_disconnectionMutex };
//         m_canceller.reset();
//         freeUnsafe();
    });
}

void HttpConnectionHandler::finalizeRead()
{
    std::lock_guard lock{ m_disconnectionMutex };
    m_canceller.reset();
    freeUnsafe();
}
