/**
  @file
  @author Stefan Frings
*/

#ifndef HTTPREQUESTHANDLER_H
#define HTTPREQUESTHANDLER_H

#include "httpglobal.h"
#include "httprequest.h"
#include "httpresponse.h"

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

class ICanceller {
public:
    virtual ~ICanceller() = default;
    virtual void cancel() = 0;
};
using CancellerRef = std::shared_ptr<ICanceller>;
using CancellerInitialization = std::function<void(CancellerRef)>;

struct ServiceParams {
    const QObject* sender;
    std::shared_ptr<const stefanfrings::HttpRequest> request;
    std::shared_ptr<HttpResponse> response;
    bool closeSocketAfterResponse;
    CancellerInitialization cancellerInitialization;
};

using FinalizeFunctor = std::function<void()>;
struct ResponseResult {
    const QObject* sender;
    std::shared_ptr<HttpResponse> response;
    FinalizeFunctor finalizer;
    bool closeSocketAfterResponse;
};

class DECLSPEC HttpRequestHandler : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(HttpRequestHandler)
public:

    /**
     * Constructor.
     * @param parent Parent object.
     */
    HttpRequestHandler(QObject* parent=nullptr);
    ~HttpRequestHandler();

signals:
    void serviceSignal(ServiceParams); // Signal to perform HttpRequestHandler::service
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

private:
    QThread* threadRequestWorker;
};

} // end of namespace

Q_DECLARE_METATYPE(stefanfrings::ServiceParams)
Q_DECLARE_METATYPE(stefanfrings::ResponseResult)

#endif // HTTPREQUESTHANDLER_H
