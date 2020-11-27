/**
  @file
  @author Alexander Nikolaev
*/

#ifndef HTTPHEADERSHANDLER_H
#define HTTPHEADERSHANDLER_H

#include <functional>
#include <vector>

#include <QString>
#include <QMap>

namespace stefanfrings {
/**
  This struct is for saving http error while headers checking was failed.
*/

struct HttpError {
    /** HTTP status code */
    int statusCode;

    /** Error text */
    QString errorText;

    void operator()(const HttpError& httpError) {
        errorText = httpError.errorText;
        statusCode = httpError.statusCode;
    }
};

typedef QMultiMap<QByteArray, QByteArray> Headers;
typedef std::tuple<bool, HttpError> HeadersCheckingStatus;
typedef std::tuple<std::vector<std::function<HeadersCheckingStatus(const Headers &)>>, HttpError> HeadersHandler;


const QByteArray &getHeaderValueRef(const Headers &container, const QByteArray &key);
} // namespace stefanfrings

#endif // HTTPHEADERSHANDLER_H
