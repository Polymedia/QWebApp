/**
  @file
  @author Stefan Frings
*/

#include "httpresponse.h"

#include <rapidjson/writer.h>

namespace {

    class StreamJSON
    {
    public:
        using Ch = char; // For rapidjson::Writer purpose.

        StreamJSON(QTcpSocket& socket)
            : m_socket(socket)
        {}

        void Put(char c) {
            if (m_idxBuffer == sizeof(m_buffer))
                sendBuffer();
            m_buffer[m_idxBuffer++] = c;
        };

        void Flush() {
            sendBuffer();
            m_socket.flush();
        };

    private:
        void sendBuffer()
        {
            const char* pData = m_buffer;
            qint64 bytesToSend = m_idxBuffer;
            m_idxBuffer = 0;

            while (m_noErrors && bytesToSend > 0 && m_socket.isOpen()) {
                if (m_socket.bytesToWrite() > sizeof(m_buffer))
                    m_socket.waitForBytesWritten(-1);

                const qint64 written = m_socket.write(pData, bytesToSend);
                m_noErrors = (written >= 0);
                if (m_noErrors) {
                    bytesToSend -= written;
                    pData += written;
                }
            }
        }

        QTcpSocket& m_socket;
        bool m_noErrors = true;
        char m_buffer[16384];
        size_t m_idxBuffer = 0;
    };

} // namespace


using namespace stefanfrings;

HttpResponse::HttpResponse(QTcpSocket *socket)
{
    this->socket=socket;
    statusCode=200;
    statusText="OK";
    sentHeaders=false;
    sentLastPart=false;
    chunkedMode=false;
}

void HttpResponse::setHeader(const QByteArray& name, const QByteArray& value)
{
    Q_ASSERT(sentHeaders==false);
    headers.insert(name,value);
}

void HttpResponse::setHeader(const QByteArray& name, int value)
{
    Q_ASSERT(sentHeaders==false);
    headers.insert(name,QByteArray::number(value));
}

const QMap<QByteArray,QByteArray>& HttpResponse::getHeaders() const
{
    return headers;
}

void HttpResponse::setStatus(int statusCode, const QByteArray& description) 
{
    this->statusCode=statusCode;
    statusText=description;
}

int HttpResponse::getStatusCode() const
{
   return this->statusCode;
}

void HttpResponse::writeHeaders()
{
    Q_ASSERT(sentHeaders==false);
    QByteArray buffer;
    buffer.append("HTTP/1.1 ");
    buffer.append(QByteArray::number(statusCode));
    buffer.append(' ');
    buffer.append(statusText);
    buffer.append("\r\n");
    foreach(const QByteArray& name, headers.keys())
    {
        buffer.append(name);
        buffer.append(": ");
        buffer.append(headers.value(name));
        buffer.append("\r\n");
    }
    foreach(const HttpCookie& cookie,cookies.values())
    {
        buffer.append("Set-Cookie: ");
        buffer.append(cookie.toByteArray());
        buffer.append("\r\n");
    }
    buffer.append("\r\n");
    writeToSocket(buffer);
    socket->flush();
    sentHeaders=true;
}

bool HttpResponse::writeToSocket(const QByteArray& data)
{
    int remaining=data.size();
    const char* ptr=data.constData();
    while (socket->isOpen() && remaining>0)
    {
        // If the output buffer has become large, then wait until it has been sent.
        if (socket->bytesToWrite()>16384)
        {
            socket->waitForBytesWritten(-1);
        }

        qint64 written=socket->write(ptr,remaining);
        if (written==-1)
        {
          return false;
        }
        ptr+=written;
        remaining-=written;
    }
    return true;
}

void HttpResponse::write(const QByteArray& data, bool lastPart)
{
    Q_ASSERT(sentLastPart==false);

    // Send HTTP headers, if not already done (that happens only on the first call to write())
    if (sentHeaders==false)
    {
        // If the whole response is generated with a single call to write(), then we know the total
        // size of the response and therefore can set the Content-Length header automatically.
        if (lastPart)
        {
           // Automatically set the Content-Length header
           headers.insert("Content-Length",QByteArray::number(data.size()));
        }
        // else if we will not close the connection at the end, them we must use the chunked mode.
        else
        {
            const QByteArray connectionValue=headers.value("Connection",headers.value("connection"));
            bool connectionClose=QString::compare(connectionValue,"close",Qt::CaseInsensitive)==0;
            if (!connectionClose)
            {
                headers.insert("Transfer-Encoding","chunked");
                chunkedMode=true;
            }
        }

        writeHeaders();
    }

    // Send data
    if (data.size()>0)
    {
        if (chunkedMode)
        {
            if (data.size()>0)
            {
                QByteArray size=QByteArray::number(data.size(),16);
                writeToSocket(size);
                writeToSocket("\r\n");
                writeToSocket(data);
                writeToSocket("\r\n");
            }
        }
        else
        {
            writeToSocket(data);
        }
    }

    // Only for the last chunk, send the terminating marker and flush the buffer.
    if (lastPart)
    {
        if (chunkedMode)
        {
            writeToSocket("0\r\n\r\n");
        }
        socket->flush();
        sentLastPart=true;
    }
}

void HttpResponse::writeJSON(const rapidjson::Document& document)
{
    writeHeaders();

    StreamJSON stream(*socket);
    rapidjson::Writer writer(stream);

    document.Accept(writer);
}

bool HttpResponse::hasSentLastPart() const
{
    return sentLastPart;
}

void HttpResponse::setCookie(const HttpCookie& cookie)
{
    Q_ASSERT(sentHeaders==false);
    if (!cookie.getName().isEmpty())
    {
        cookies.insert(cookie.getName(),cookie);
    }
}


const QMap<QByteArray,HttpCookie>& HttpResponse::getCookies() const
{
    return cookies;
}


void HttpResponse::redirect(const QByteArray& url)
{
    setStatus(303,"See Other");
    setHeader("Location",url);
    write("Redirect",true);
}


void HttpResponse::flush()
{
    socket->flush();
}


bool HttpResponse::isConnected() const
{
    return socket->isOpen();
}
