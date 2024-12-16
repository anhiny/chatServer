#include "chatservice.h"
#include "public.h"
#include "muduo/base/Logging.h"
#include <vector>

using namespace std;
using namespace muduo;

ChatService *ChatService::instance() {
    static ChatService service;
    return &service;
}


// 注册对应的回调操作
ChatService::ChatService() {
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::creatGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});


    // 连接redis服务器
    if (_redis.connect()) {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}


// 服务器异常，业务重置业务
void ChatService::reset() {
    // 把所有online状态，设置为offline
    _userModel.resetState();
}


// 获取对应的回调函数

MsgHandler ChatService::getHandler(int msgid) {
    // 记录错误日志， msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end()) {
        // 返回一个默认处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp) {
            LOG_ERROR << "msgid" << msgid << " can not find handler!";
        };
    } else {
        return _msgHandlerMap[msgid];
    }
}

// 登陆回调
void ChatService::login(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int id = js["id"].get<int>();
    string pwd = js["password"];


    User user = _userModel.query(id);
    if (user.getId() != -1 && user.getPwd() == pwd) {
        if (user.getState() == "online") {
            // 该用户已经登陆，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";
            conn->send(response.dump());
        } else {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id);


            // 登录成功, 更新用户状态信息
            user.setState("online");
            _userModel.updateState(user);
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty()) {
                response["offlinemsg"] = vec;
                // 读取离线消息后就删除记录
                _offlineMsgModel.remove(id);
            }
            // 查询用户好友信息并返回

            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty()) {
                vector<string> vec2;
                for (User &user: userVec) {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            conn->send(response.dump());
        }
    } else {
        // 该用户不存在，用户存在但是密码错误，登陆失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid";
        conn->send(response.dump());
    }
}

// 注册回调  name password
void ChatService::reg(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state) {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    } else {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}


// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn) {
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it) {
            if (it->second == conn) {
                // 从map表删除用户连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    // 更新用户状态信息
    if (user.getId() != -1) {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 用户注销业务
void ChatService::loginout(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int useid = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(useid);
        if (it != _userConnMap.end()) {
            _userConnMap.erase(it);
        }
    }
    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(useid);

    User user(useid, "", "", "offline");
    _userModel.updateState(user);

}

void ChatService::oneChat(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int toId = js["toid"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toId);
        if (it != _userConnMap.end()) {
            // toId在线，转发消息
            it->second->send(js.dump());
            return;
        }
    }

    // 查询toid是否在线
    User user = _userModel.query(toId);
    if (user.getState() == "online") {
        _redis.publish(toId, js.dump());
        return;
    }

    // toId不在线
    _offlineMsgModel.insert(toId, js.dump());

}


// 添加好友业务
void ChatService::addFriend(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int userId = js["id"].get<int>();
    int friendId = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userId, friendId);
}


// 创建群组业务
void ChatService::creatGroup(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int useid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新的群组信息
    Group group(-1, name, desc);
    if (_groupModel.creatGroup(group)) {
        _groupModel.addGroup(useid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}


// 群组聊天业务
void ChatService::groupChat(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);
    lock_guard<mutex> lock(_connMutex);
    for (int id: useridVec) {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end()) {
            it->second->send(js.dump());
        } else {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online") {
                _redis.publish(id, js.dump());
                return;
            } else {
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}


// 从redis消息队列中获取订阅消息
void ChatService::handleRedisSubscribeMessage(int userid, std::string msg) {
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end()) {
        it->second->send(msg);
        return;
    }

    // 存储用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}















































