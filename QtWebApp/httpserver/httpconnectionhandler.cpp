/**
  @file
  @author Stefan Frings
*/

#include "httpconnectionhandler.h"
#include "httpresponse.h"
#include <future>

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
    connect(socket, SIGNAL(disconnected()), SLOT(disconnected()));
    connect(&readTimer, SIGNAL(timeout()), SLOT(readTimeout()));
    connect(thread, SIGNAL(finished()), this, SLOT(thread_done()));

    connect(this, &HttpConnectionHandler::queueFunctionSignal, this, &HttpConnectionHandler::onQueueFunctionSignal, Qt::QueuedConnection);
    connect(requestHandler, &HttpRequestHandler::responseResultSignal, this, &HttpConnectionHandler::onResponseResultSignal, Qt::QueuedConnection);

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
    setBusy();
    currentRequestID = 0;
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
    startTimer();
    // delete previous request
    resetCurrentRequest();
}


bool HttpConnectionHandler::isBusy() const
{
    return busy;
}

void HttpConnectionHandler::setBusy(bool isBusy /*= true*/)
{
    this->busy = isBusy;
}

void stefanfrings::HttpConnectionHandler::setHeadersHandler(HeadersHandler headersHandler)
{
    std::lock_guard lock{ headersHandlerMutex };
    this->headersHandler = headersHandler;
}

void HttpConnectionHandler::disconnectFromHost()
{
    while (socket->bytesToWrite())
        socket->waitForBytesWritten();
    socket->disconnectFromHost();
    resetCurrentRequest();
}

void HttpConnectionHandler::startTimer()
{  
    const int readTimeout = settings->value("readTimeout", 10000).toInt();
    readTimer.start(readTimeout);
}

void stefanfrings::HttpConnectionHandler::resetCurrentRequest()
{
    currentRequestID = 0;
    currentRequest.reset();
}

void HttpConnectionHandler::onResponseResultSignal(ResponseResult responseResult)
{
    auto onException = [this](const char* message) {
        qWarning() << "Exception:" << message;
        if (socket)
            socket->disconnectFromHost();
    };

    if (responseResult.requestID == currentRequestID) {
        try {
            if (responseResult.finalizer)
                responseResult.finalizer();

            if (WriteToSocket::YES == responseResult.isWriteToSocket)
                finalizeResponse(responseResult.response, responseResult.closeSocketAfterResponse);
        }
        catch (const std::exception& e) {
            onException(e.what());
        }
        catch (...) {
            onException("Unknown");
        }
    }
}

void HttpConnectionHandler::socketSafeExecution(QueuedFunction function)
{
    std::promise<void> promise;
    auto future = promise.get_future();

    auto queuedFunction = [function, &promise] {
        try {
            function();
            promise.set_value();
        }
        catch (...) {
            promise.set_exception(std::current_exception());
        }
    };

    emit queueFunctionSignal(queuedFunction);

    future.get();
}

void HttpConnectionHandler::onQueueFunctionSignal(QueuedFunction function)
{
    function();
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
    currentRequestID = 0;
    socket->close();
    readTimer.stop();
    setBusy(false);

    CancellerRef canceller;
    {
        std::lock_guard lck{ m_cancellerMutex };
        canceller = m_canceller;
    }

    if (canceller)
        canceller->cancel();
}

static std::atomic<uint64_t> reguestID = 1;

void HttpConnectionHandler::read()
{
    // The loop adds support for HTTP pipelinig
    while (socket->bytesAvailable())
    {
        #ifdef SUPERVERBOSE
        qDebug("HttpConnectionHandler (%p): read input", static_cast<void*>(this));
        #endif

        // Create new HttpRequest object if necessary
        if (!currentRequest) {
            std::lock_guard lock{ headersHandlerMutex };
            currentRequest = std::make_shared<HttpRequest>(settings, headersHandler);
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
                startTimer();
            }
        }

        switch (currentRequest->getStatus()) {
            default: break;

            // If some headers fails checking, return status code and error text from handler
            case HttpRequest::wrongHeaders: {
                const auto [statusCode, text] = currentRequest->getHttpError();
                const QString response = QString{ "HTTP/1.1 %1\r\nConnection: "
                                                 "close\r\n\r\n%2\r\n" }
                                        .arg(statusCode)
                                        .arg(text);

                socket->write(response.toUtf8().constData());
                disconnectFromHost();
                return;
            }

            // If the request is aborted, return error message and close the connection
            case HttpRequest::abort:
                socket->write("HTTP/1.1 413 entity too large\r\nConnection: close\r\n\r\n413 Entity too large\r\n");
                disconnectFromHost();
                return;

            // If the request is complete, let the request mapper dispatch it
            case HttpRequest::complete: {
                readTimer.stop();
                qDebug("HttpConnectionHandler (%p): received request", static_cast<void*>(this));

                // Copy the Connection:close header to the response
                auto response = std::make_shared<HttpResponse>(socket, *this);
                bool closeConnection=QString::compare(currentRequest->getHeader("Connection"), "close", Qt::CaseInsensitive) == 0;
                if (!closeConnection)
                    // In case of HTTP 1.0 protocol add the Connection:close header.
                    // This ensures that the HttpResponse does not activate chunked mode, which is not spported by HTTP 1.0.
                    closeConnection = QString::compare(currentRequest->getVersion(), "HTTP/1.0", Qt::CaseInsensitive) == 0;

                if (closeConnection)
                    response->setHeader("Connection", "close");

                // Call the request mapper
                auto onInitCanceller = [this](CancellerRef ref) {
                    std::lock_guard lock{ m_cancellerMutex };
                    m_canceller = ref;
                };
                currentRequestID = reguestID++;

                auto fnSendError = [this](const char * msg) {
                    qWarning() << "Exception on callService:" << msg;
                    const auto response = QString("HTTP/1.1 500 error on callService \r\nException: %1").arg(msg);
                    socket->write(response.toUtf8().constData());
                    disconnectFromHost();
                };

                try {
                    requestHandler->callService(ServiceParams{ currentRequestID, std::make_shared<HttpRequest>(*currentRequest) /*request copy*/, response, closeConnection ? CloseSocket::YES : CloseSocket::NO, onInitCanceller });
                }
                catch (const std::exception& e) {
                    fnSendError(e.what());
                }
                catch (...) {
                    fnSendError("Unknown");
                }
            }
        }
    }
}

void HttpConnectionHandler::finalizeResponse(std::shared_ptr<HttpResponse> response, CloseSocket isCloseConnection)
{
    bool closeConnection = CloseSocket::YES == isCloseConnection;

    // Finalize sending the response if not already done
    if (!response->hasSentLastPart())
        response->write(QByteArray(), true);

    qDebug("HttpConnectionHandler (%p): finished request", static_cast<void*>(this));

    // Find out whether the connection must be closed
    if (!closeConnection) {
        // Maybe the request handler or mapper added a Connection:close header in the meantime
        closeConnection = QString::compare(response->getHeaders().value("Connection"), "close", Qt::CaseInsensitive)==0;
        if (!closeConnection)
        {
            // If we have no Content-Length header and did not use chunked mode, then we have to close the
            // connection to tell the HTTP client that the end of the response has been reached.
            bool hasContentLength=response->getHeaders().contains("Content-Length");
            if (!hasContentLength)
                closeConnection = 0 != QString::compare(response->getHeaders().value("Transfer-Encoding"), "chunked", Qt::CaseInsensitive);
        }
    }

    // Close the connection or prepare for the next request on the same connection.
    if (closeConnection)
    {
        disconnectFromHost();
    }
    else
    {
        startTimer();
    }
    
    resetCurrentRequest();
}