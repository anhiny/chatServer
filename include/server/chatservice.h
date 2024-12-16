#ifndef CHAT_CHATSERVICE_H
#define CHAT_CHATSERVICE_H

#include <unordered_map>
#include <functional>
#include <muduo/net/TcpServer.h>
#include "json.hpp"
#include "server/model/usermodel.h"
#include <mutex>
#include "server/model/offlinemessagemodel.h"
#include "server/model/friendmodel.h"
#include "server/model/groupmodel.h"
#include "redis.h"


using namespace std;
using namespace muduo;
using namespace placeholders;
using namespace muduo::net;
using json = nlohmann::json;

// 处理消息的事件回调方法类型
using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;

// 聊天服务器业务类

class ChatService {
public:
    // 获取单例对象接口函数
    static ChatService *instance();

    // 登陆业务
    void login(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 注册业务
    void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 一对一聊天业务
    void oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 添加好友业务
    void addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 创建群聊业务
    void creatGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 加入群聊业务
    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 群组聊天服务
    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // 注销业务
    void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);

    // redis订阅消息业务
    void handleRedisSubscribeMessage(int userid, string msg);

    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);

    // 服务器异常，异常重置方法
    void reset();

    // 处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr &conn);


private:
    ChatService();

    // 存储消息id和对应回调函数
    unordered_map<int, MsgHandler> _msgHandlerMap;

    // 数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    // 存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> _userConnMap;

    // 互斥锁，保证userConnMap线程安全
    mutex _connMutex;

    // redis操作对象
    Redis _redis;
};


#endif //CHAT_CHATSERVICE_H
