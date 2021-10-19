/**
  @file
  @author Stefan Frings
*/

#include "httprequesthandler.h"
#include "QThread"

using namespace stefanfrings;

HttpRequestHandler::HttpRequestHandler(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ServiceParams>("ServiceParams");
    qRegisterMetaType<ResponseResult>("ResponseResult");

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
    service(params);
}


