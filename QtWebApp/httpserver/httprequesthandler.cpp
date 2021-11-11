/**
  @file
  @author Stefan Frings
*/

#include "httprequesthandler.h"
#include <QtConcurrent/QtConcurrentRun>

using namespace stefanfrings;

HttpRequestHandler::HttpRequestHandler(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<stefanfrings::ServiceParams>("ServiceParams");
    qRegisterMetaType<stefanfrings::ResponseResult>("ResponseResult");

    connect(this, &HttpRequestHandler::serviceSignal, this, &HttpRequestHandler::serviceSlot, Qt::QueuedConnection);

    threadRequestWorker = new QThread();
    threadRequestWorker->start();
    moveToThread(threadRequestWorker);
}

HttpRequestHandler::~HttpRequestHandler()
{
    threadRequestWorker->quit();
    threadRequestWorker->wait();
    threadRequestWorker->deleteLater();
}

void HttpRequestHandler::service(ServiceParams params)
{
    auto& request = params.request;
    auto& response = params.response;

    qCritical("HttpRequestHandler: you need to override the service() function");
    qDebug("HttpRequestHandler: request=%s %s %s", request->getMethod().data(), request->getPath().data(), request->getVersion().data());
    response->setStatus(501,"not implemented");
    response->write("501 not implemented",true);
}

void HttpRequestHandler::serviceSlot(ServiceParams params)
{
    QtConcurrent::run(QThreadPool::globalInstance(), [this, params] {
        try {
            service(params);
        }
        catch (const std::exception& ex) {
            qCritical("HttpConnectionHandler (%p): An uncatched exception occured in the request handler: %s",
                static_cast<void*>(this), ex.what());
        }
        catch (...) {
            qCritical("HttpConnectionHandler (%p): An uncatched exception occured in the request handler",
                static_cast<void*>(this));
        }
    });
}


