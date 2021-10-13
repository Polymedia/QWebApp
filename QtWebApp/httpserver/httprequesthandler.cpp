/**
  @file
  @author Stefan Frings
*/

#include "httprequesthandler.h"

using namespace stefanfrings;

HttpRequestHandler::HttpRequestHandler(QObject* parent)
    : QObject(parent)
{}

std::future<QVariant> HttpRequestHandler::service(const HttpRequest& request, std::shared_ptr<stefanfrings::HttpResponse> response)
{
    qCritical("HttpRequestHandler: you need to override the service() function");
    qDebug("HttpRequestHandler: request=%s %s %s",request.getMethod().data(),request.getPath().data(),request.getVersion().data());
    response->setStatus(501,"not implemented");
    response->write("501 not implemented",true);

    std::promise<QVariant> promise;

    auto result = promise.get_future();
    promise.set_value({});

    return result;
}
