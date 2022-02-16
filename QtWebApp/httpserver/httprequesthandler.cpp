/**
  @file
  @author Stefan Frings
*/

#include "httprequesthandler.h"
#include <QtConcurrent/QtConcurrentRun>
#include <thread>

using namespace stefanfrings;

static const auto isRegisteredServiceParams = qRegisterMetaType<ServiceParams>("ServiceParams");
static const auto isRegisteredResponseResult = qRegisterMetaType<ResponseResult>("ResponseResult");

HttpRequestHandler::HttpRequestHandler(QObject* parent)
    : QObject(parent)
{}

void HttpRequestHandler::service(ServiceParams params)
{
    auto& request = params.request;
    auto& response = params.response;

    qCritical("HttpRequestHandler: you need to override the service() function");
    qDebug("HttpRequestHandler: request=%s %s %s", request->getMethod().data(), request->getPath().data(), request->getVersion().data());
    response->setStatus(501,"not implemented");
    response->write("501 not implemented",true);
}

void HttpRequestHandler::callService(ServiceParams params)
{
    QtConcurrent::run([this, params] {
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


