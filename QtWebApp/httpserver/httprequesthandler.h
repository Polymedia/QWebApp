/**
  @file
  @author Stefan Frings
*/

#ifndef HTTPREQUESTHANDLER_H
#define HTTPREQUESTHANDLER_H

#include "httpglobal.h"
#include "httprequest.h"
#include "httpresponse.h"
#include <future>

namespace stefanfrings {

/**
   The request handler generates a response for each HTTP request. Web Applications
   usually have one central request handler that maps incoming requests to several
   controllers (servlets) based on the requested path.
   <p>
   You need to override the service() method or you will always get an HTTP error 501.
   <p>
   @warning Be aware that the main request handler instance must be created on the heap and
   that it is used by multiple threads simultaneously.
   @see StaticFileController which delivers static local files.
*/

struct ServiceParams {
    std::shared_ptr <const stefanfrings::HttpRequest> request;
    std::shared_ptr<HttpResponse> response;
    bool closeConnection;
};
Q_DECLARE_METATYPE(ServiceParams)

using FinalizeFunctor = std::function<void()>;
struct ResponseResult {
    std::shared_ptr<HttpResponse> response;
    FinalizeFunctor finalizer;
    bool closeConnection;
};
Q_DECLARE_METATYPE(ResponseResult)

class DECLSPEC HttpRequestHandler : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(HttpRequestHandler)
public:

    /**
     * Constructor.
     * @param parent Parent object.
     */
    HttpRequestHandler(QObject* parent=nullptr);

signals:
    void serviceSignal(ServiceParams);
    void responseResultSignal(ResponseResult);

private slots:
    void serviceSlot(ServiceParams params);

protected:
/**
  Generate a response for an incoming HTTP request.
  @param request The received HTTP request
  @param response Must be used to return the response
  @warning This method must be thread safe
*/
    virtual void service(ServiceParams params);

};

} // end of namespace

#endif // HTTPREQUESTHANDLER_H
