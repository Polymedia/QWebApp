/**
  @file
  @author Stefan Frings
*/

#include "httprequesthandler.h"

using namespace stefanfrings;

HttpRequestHandler::HttpRequestHandler(QObject* parent)
    : QObject(parent)
{
    connect(this, &HttpRequestHandler::serviceSignal, this, &HttpRequestHandler::serviceSlot);
    qRegisterMetaType<ServiceParams>("ServiceParams");
    qRegisterMetaType<ResponseResult>("ResponseResult");
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
