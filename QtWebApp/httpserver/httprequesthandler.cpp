/**
  @file
  @author Stefan Frings
*/

#include "httprequesthandler.h"

using namespace stefanfrings;

HttpRequestHandler::HttpRequestHandler(QObject* parent)
    : QObject(parent)
{}

void HttpRequestHandler::service(const HttpRequest& request, HttpResponse& response, CancellerInitialization /*onInitCanceller*/)
{
    qCritical("HttpRequestHandler: you need to override the service() function");
    qDebug("HttpRequestHandler: request=%s %s %s",request.getMethod().data(),request.getPath().data(),request.getVersion().data());
    response.setStatus(501,"not implemented");
    response.write("501 not implemented",true);
}
