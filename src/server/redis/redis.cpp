#include "redis.h"
#include <iostream>

using namespace std;

bool Redis::connect() {
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _publish_context) {
        cerr << "connect redis failed!" << endl;
        return false;
    }
    _subscribe_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _subscribe_context) {
        cerr << "connect redis failed!" << endl;
        return false;
    }
    // 在单独线程中，监听通道上的事件，有消息给业务层上报
    thread t([&]() {
        observer_channel_message();
    });
    t.detach();

    cout << "connect redis-server success!" << endl;

    return true;
}

bool Redis::publish(int channel, std::string message) {
    redisReply *reply = (redisReply *) redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply) {
        cerr << "publish command failed!" << endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::subscribe(int channel) {
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面的消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observe_channel_message函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源
    if (REDIS_ERR == redisAppendCommand(this->_subscribe_context, "SUBSCRIBE %d", channel)) {
        cerr << "subscribe command failed!" << endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区发送完毕(done被置为1)
    int done = 0;
    while (!done) {
        if (REDIS_ERR == redisBufferWrite(this->_subscribe_context, &done)) {
            cerr << "subscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

bool Redis::unsubscribe(int channel) {
    if (REDIS_ERR == redisAppendCommand(this->_subscribe_context, "UNSUBSCRIBE %d", channel)) {
        cerr << "unsubscribe command failed!" << endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，知道缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done) {
        if (REDIS_ERR == redisBufferWrite(this->_subscribe_context, &done)) {
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

void Redis::observer_channel_message() {
    redisReply *reply = nullptr;
    while (REDIS_OK == redisGetReply(this->_subscribe_context, (void **) &reply)) {
        // 订阅收到的消息是一个带三元组的数组
        if (reply != nullptr && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
            // 确保所有元素都存在且有效
            if (reply->element[1] != nullptr && reply->element[1]->type == REDIS_REPLY_STRING && 
                reply->element[2] != nullptr && reply->element[2]->type == REDIS_REPLY_STRING) {
                // 确保回调函数已设置
                if (_notify_message_handler) {
                    // 给业务层上报通道上发生的消息
                    _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
                } else {
                    cerr << "Error: notify message handler not set!" << endl;
                }
            }
        }

        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }

    cerr << ">>>>>>>>>>>>>>>>>>>>> observe_channel_message quit <<<<<<<<<<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int, std::string)> fn) {
    this->_notify_message_handler = fn;
}






































