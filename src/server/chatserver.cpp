#include "chatserver.h"
#include "chatservice.h"
#include <functional>
#include <string>
#include "json.hpp"

using namespace std;
using namespace placeholders;
using json = nlohmann::json;

ChatServer::ChatServer(muduo::net::EventLoop *loop, const muduo::net::InetAddress &listenAddr,
                       const std::string &nameArg) : _server(loop, listenAddr, nameArg), _loop(loop) {
    // 注册连接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    // 添加好友回调

    // 设置线程数量
    _server.setThreadNum(4);
};

// 启动服务
void ChatServer::start() {
    _server.start();
}

void ChatServer::onConnection(const muduo::net::TcpConnectionPtr &conn) {
    // 客户端断开连接
    if (!conn->connected()) {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

void
ChatServer::onMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp time) {
    string buf = buffer->retrieveAllAsString();
    // 数据解码：反序列化
    json js = json::parse(buf);
    auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // 业务处理
    msgHandler(conn, js, time);
}