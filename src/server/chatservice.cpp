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
ChatService::ChatService() : _threadPool(8) {  // 初始化线程池，最小8个线程
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::creatGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 先设置Redis回调，再连接Redis服务器
    _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    
    // 连接redis服务器
    if (!_redis.connect()) {
        cerr << "Redis connect failed!" << endl;
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

    // 使用线程池异步处理登录请求
    _threadPool.addTask([=]() {
        // 检查连接是否仍然有效
        if (!conn->connected()) {
            LOG_ERROR << "Connection is closed for user id: " << id;
            return;
        }

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
                bool redis_success = _redis.subscribe(id);
                if (!redis_success) {
                    LOG_ERROR << "Redis subscribe failed for user id: " << id;
                }

                // 登录成功, 更新用户状态信息
                user.setState("online");
                _userModel.updateState(user);
                json response;
                response["msgid"] = LOGIN_MSG_ACK;
                response["errno"] = 0;
                response["id"] = user.getId();
                response["name"] = user.getName();
                
                try {
                    // 查询该用户是否有离线消息
                    vector<string> vec = _offlineMsgModel.query(id);
                    if (!vec.empty()) {
                        response["offlinemsg"] = vec;
                        // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                        _offlineMsgModel.remove(id);
                    }
                    
                    // 查询该用户的好友信息并返回
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
                    
                    // 查询用户的群组信息
                    vector<Group> groupuserVec = _groupModel.query(id);
                    if (!groupuserVec.empty()) {
                        // group:[{groupid:[xxx, xxx, xxx]}]
                        vector<string> groupV;
                        for (Group &group: groupuserVec) {
                            json grpjson;
                            grpjson["id"] = group.getId();
                            grpjson["groupname"] = group.getName();
                            grpjson["groupdesc"] = group.getDesc();
                            vector<string> userV;
                            for (GroupUser &user: group.getUsers()) {
                                json js;
                                js["id"] = user.getId();
                                js["name"] = user.getName();
                                js["state"] = user.getState();
                                js["role"] = user.getRole();
                                userV.push_back(js.dump());
                            }
                            grpjson["users"] = userV;
                            groupV.push_back(grpjson.dump());
                        }
                        response["groups"] = groupV;
                    }
                } catch (const exception& e) {
                    LOG_ERROR << "Exception in login for user id: " << id << ", error: " << e.what();
                    response["errno"] = 1;
                    response["errmsg"] = "服务器内部错误";
                    conn->send(response.dump());
                    return;
                }
                
                // 检查连接是否仍然有效
                if (conn->connected()) {
                    conn->send(response.dump());
                } else {
                    LOG_ERROR << "Connection is closed for user id: " << id;
                    // 用户连接已断开，将用户状态设置为离线
                    user.setState("offline");
                    _userModel.updateState(user);
                    // 从map中移除连接
                    lock_guard<mutex> lock(_connMutex);
                    _userConnMap.erase(id);
                }
            }
        } else {
            // 该用户不存在，登录失败
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 1;
            response["errmsg"] = "id or password is invalid!";
            
            // 检查连接是否仍然有效
            if (conn->connected()) {
                conn->send(response.dump());
            } else {
                LOG_ERROR << "Connection is closed for user id: " << id;
            }
        }
    });
}

// 注册回调  name password
void ChatService::reg(const muduo::net::TcpConnectionPtr &conn, json &js, muduo::Timestamp time) {
    string name = js["name"];
    string pwd = js["password"];

    // 使用线程池异步处理注册请求
    _threadPool.addTask([=]() {
        // 检查连接是否仍然有效
        if (!conn->connected()) {
            LOG_ERROR << "Connection is closed during registration for user: " << name;
            return;
        }

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
            
            // 检查连接是否仍然有效
            if (conn->connected()) {
                conn->send(response.dump());
            } else {
                LOG_ERROR << "Connection is closed after registration for user id: " << user.getId();
            }
        } else {
            // 注册失败
            json response;
            response["msgid"] = REG_MSG_ACK;
            response["errno"] = 1;
            
            // 检查连接是否仍然有效
            if (conn->connected()) {
                conn->send(response.dump());
            } else {
                LOG_ERROR << "Connection is closed after registration failure for user: " << name;
            }
        }
    });
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
    if (user.getId() != -1) {
        try {
            _redis.unsubscribe(user.getId());
        } catch (const exception& e) {
            LOG_ERROR << "Exception in unsubscribe for user id: " << user.getId() << ", error: " << e.what();
        }

        // 更新用户状态信息
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
    
    // 使用线程池异步处理聊天消息
    _threadPool.addTask([=]() {
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

        // toId不在线，存储离线消息
        try {
            _offlineMsgModel.insert(toId, js.dump());
        } catch (const exception& e) {
            LOG_ERROR << "Exception in oneChat when inserting offline message for user id: " << toId << ", error: " << e.what();
        }
    });
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
    
    // 使用线程池异步处理群聊消息
    _threadPool.addTask([=]() {
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
                } else {
                    try {
                        _offlineMsgModel.insert(id, js.dump());
                    } catch (const exception& e) {
                        LOG_ERROR << "Exception in groupChat when inserting offline message for user id: " << id << ", error: " << e.what();
                    }
                }
            }
        }
    });
}


// 从redis消息队列中获取订阅消息
void ChatService::handleRedisSubscribeMessage(int userid, std::string msg) {
    // 使用线程池异步处理Redis订阅消息
    _threadPool.addTask([=]() {
        try {
            lock_guard<mutex> lock(_connMutex);
            auto it = _userConnMap.find(userid);
            if (it != _userConnMap.end()) {
                TcpConnectionPtr conn = it->second;
                if (conn && conn->connected()) {
                    conn->send(msg);
                    return;
                }
            }

            // 存储用户的离线消息
            _offlineMsgModel.insert(userid, msg);
        } catch (const exception& e) {
            LOG_ERROR << "Exception in handleRedisSubscribeMessage for user id: " << userid << ", error: " << e.what();
        }
    });
}















































