#include "httpheadershandler.h"

const QByteArray &stefanfrings::getHeaderValueRef(const Headers &container, const QByteArray &key) {
    const static QByteArray EMPTY_VALUE{};

    const auto it = container.find(key);

    if (container.end() == it)
        return EMPTY_VALUE;

    return it.value();
}
