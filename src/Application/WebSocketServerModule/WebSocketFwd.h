// For conditions of distribution and use, see copyright notice in LICENSE

#pragma once

#include "CoreTypes.h"

#include <QHash>
#include <QObject>
#include <QVariant>
#include <QString>
#include <QStringList>

#include <vector>

class WebSocketServerModule;

namespace WebSocket
{
    class Server;
    class Handler;
    class UserConnection;
    
    typedef shared_ptr<UserConnection> UserConnectionPtr;
    typedef std::vector<UserConnectionPtr> UserConnectionList;
}

class Framework;

struct libwebsocket_context;

typedef shared_ptr<WebSocket::Server> WebSocketServerPtr;
typedef shared_ptr<WebSocket::UserConnection> WebSocketUserConnectionPtr;

