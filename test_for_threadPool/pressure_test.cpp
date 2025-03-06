#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "json.hpp"
#include "public.h"

using namespace std;
using json = nlohmann::json;

// 统计信息
struct Statistics {
    atomic<int> successCount{0};
    atomic<int> failCount{0};
    atomic<int> totalMsgCount{0};
    atomic<long long> totalResponseTime{0};
    atomic<int> regFailCount{0};
    atomic<int> loginFailCount{0};
    atomic<int> msgFailCount{0};
    
    mutex outputMutex;
    
    void printStats(int durationSeconds) {
        lock_guard<mutex> lock(outputMutex);
        cout << "====== 测试结果 ======" << endl;
        cout << "总连接数: " << successCount + failCount << endl;
        cout << "成功连接: " << successCount << endl;
        cout << "失败连接: " << failCount << endl;
        cout << "注册失败: " << regFailCount << endl;
        cout << "登录失败: " << loginFailCount << endl;
        cout << "消息失败: " << msgFailCount << endl;
        cout << "总消息数: " << totalMsgCount << endl;
        cout << "平均响应时间: " << (totalMsgCount > 0 ? totalResponseTime / totalMsgCount : 0) << "ms" << endl;
        cout << "每秒处理消息数: " << totalMsgCount / durationSeconds << endl;
    }
};

// 调试日志
mutex debugMutex;
void debugLog(const string& message) {
    lock_guard<mutex> lock(debugMutex);
    cout << "[DEBUG] " << message << endl;
}

// 模拟客户端行为
void simulateClient(const string& serverIp, int port, int msgPerClient, Statistics& stats, bool debug = false) {
    // 创建socket连接
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        stats.failCount++;
        return;
    }
    
    // 连接服务器
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());
    
    if (connect(clientfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        close(clientfd);
        stats.failCount++;
        return;
    }
    
    stats.successCount++;
    
    // 生成随机用户名和密码
    int randomNum = rand() % 10000 + 1000; // 随机数
    string username = "test_user_" + to_string(randomNum);
    string password = "password";
    
    if (debug) debugLog("创建用户: " + username);
    
    // 注册用户
    json regJson;
    regJson["msgid"] = REG_MSG;
    regJson["name"] = username;
    regJson["password"] = password;
    string regMsg = regJson.dump();
    
    if (debug) debugLog("发送注册请求: " + regMsg);
    
    // 发送注册消息
    if (send(clientfd, regMsg.c_str(), regMsg.length() + 1, 0) == -1) {
        if (debug) debugLog("发送注册请求失败");
        close(clientfd);
        return;
    }
    
    // 接收响应
    char buffer[1024] = {0};
    int len = recv(clientfd, buffer, 1024, 0);
    if (len <= 0) {
        if (debug) debugLog("接收注册响应失败");
        close(clientfd);
        return;
    }
    
    if (debug) debugLog("收到注册响应: " + string(buffer));
    
    // 解析注册响应，获取用户ID
    json responseJson;
    try {
        responseJson = json::parse(buffer);
    } catch (const exception& e) {
        if (debug) debugLog("解析注册响应JSON失败: " + string(e.what()));
        close(clientfd);
        stats.regFailCount++;
        return;
    }
    
    // 检查注册是否成功
    if (!responseJson.contains("errno") || responseJson["errno"] != 0) {
        if (debug) debugLog("注册失败: " + (responseJson.contains("errmsg") ? responseJson["errmsg"].get<string>() : "未知错误"));
        close(clientfd);
        stats.regFailCount++;
        return;
    }
    
    // 检查是否包含id字段
    if (!responseJson.contains("id") || responseJson["id"].is_null()) {
        if (debug) debugLog("注册响应中缺少有效的id字段");
        close(clientfd);
        stats.regFailCount++;
        return;
    }
    
    // 从注册响应中获取用户ID
    int userId = responseJson["id"].get<int>();
    if (debug) debugLog("注册成功，用户ID: " + to_string(userId));
    
    // 登录
    json loginJson;
    loginJson["msgid"] = LOGIN_MSG;
    loginJson["id"] = userId;
    loginJson["password"] = password;
    string loginMsg = loginJson.dump();
    
    if (debug) debugLog("发送登录请求: " + loginMsg);
    
    if (send(clientfd, loginMsg.c_str(), loginMsg.length() + 1, 0) == -1) {
        if (debug) debugLog("发送登录请求失败");
        close(clientfd);
        return;
    }
    
    memset(buffer, 0, sizeof(buffer));
    len = recv(clientfd, buffer, 1024, 0);
    if (len <= 0) {
        if (debug) debugLog("接收登录响应失败");
        close(clientfd);
        return;
    }
    
    if (debug) debugLog("收到登录响应: " + string(buffer));
    
    // 解析登录响应
    json loginResponse;
    try {
        loginResponse = json::parse(buffer);
    } catch (const exception& e) {
        if (debug) debugLog("解析登录响应JSON失败: " + string(e.what()));
        close(clientfd);
        stats.loginFailCount++;
        return;
    }
    
    if (!loginResponse.contains("errno") || loginResponse["errno"] != 0) {
        if (debug) debugLog("登录失败: " + (loginResponse.contains("errmsg") ? loginResponse["errmsg"].get<string>() : "未知错误"));
        close(clientfd);
        stats.loginFailCount++;
        return;
    }
    
    if (debug) debugLog("登录成功");
    
    // 发送测试消息
    for (int i = 0; i < msgPerClient; i++) {
        json chatJson;
        chatJson["msgid"] = ONE_CHAT_MSG;
        chatJson["id"] = userId;
        chatJson["from"] = username;
        chatJson["to"] = "test_user_" + to_string(rand() % 10000 + 1000);
        chatJson["msg"] = "压力测试消息-" + to_string(i);
        string chatMsg = chatJson.dump();
        
        if (debug) debugLog("发送聊天消息: " + chatMsg);
        
        auto start = chrono::high_resolution_clock::now();
        
        if (send(clientfd, chatMsg.c_str(), chatMsg.length() + 1, 0) == -1) {
            if (debug) debugLog("发送聊天消息失败");
            stats.msgFailCount++;
            break;
        }
        
        memset(buffer, 0, sizeof(buffer));
        len = recv(clientfd, buffer, 1024, 0);
        if (len <= 0) {
            if (debug) debugLog("接收聊天响应失败");
            stats.msgFailCount++;
            break;
        }
        
        if (debug) debugLog("收到聊天响应: " + string(buffer));
        
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        
        stats.totalResponseTime += duration;
        stats.totalMsgCount++;
        
        // 模拟思考时间
        this_thread::sleep_for(chrono::milliseconds(rand() % 100));
    }
    
    // 注销
    json logoutJson;
    logoutJson["msgid"] = LOGINOUT_MSG;
    logoutJson["id"] = userId;
    string logoutMsg = logoutJson.dump();
    
    if (debug) debugLog("发送注销请求: " + logoutMsg);
    
    send(clientfd, logoutMsg.c_str(), logoutMsg.length() + 1, 0);
    
    close(clientfd);
    if (debug) debugLog("客户端会话结束");
}

int main(int argc, char** argv) {
    if (argc < 5) {
        cout << "Usage: " << argv[0] << " <server_ip> <server_port> <client_count> <test_duration_seconds> [messages_per_client] [debug_mode]" << endl;
        return -1;
    }
    
    string serverIp = argv[1];
    int serverPort = atoi(argv[2]);
    int clientCount = atoi(argv[3]);
    int testDuration = atoi(argv[4]);
    int msgPerClient = (argc > 5) ? atoi(argv[5]) : 10;
    bool debugMode = (argc > 6) ? (atoi(argv[6]) == 1) : false;
    
    // 初始化随机数种子
    srand(time(nullptr));
    
    cout << "开始压力测试..." << endl;
    cout << "服务器IP: " << serverIp << endl;
    cout << "服务器端口: " << serverPort << endl;
    cout << "并发客户端数: " << clientCount << endl;
    cout << "测试持续时间: " << testDuration << "秒" << endl;
    cout << "每客户端消息数: " << msgPerClient << endl;
    cout << "调试模式: " << (debugMode ? "开启" : "关闭") << endl;
    
    Statistics stats;
    vector<thread> threads;
    
    // 如果开启调试模式，只创建一个客户端并打印详细日志
    if (debugMode) {
        simulateClient(serverIp, serverPort, msgPerClient, stats, true);
    } else {
        // 创建并发客户端
        for (int i = 0; i < clientCount; i++) {
            threads.emplace_back(simulateClient, serverIp, serverPort, msgPerClient, ref(stats), false);
            // 控制客户端创建速率，避免突发连接
            this_thread::sleep_for(chrono::milliseconds(50));
        }
        
        // 设置测试持续时间
        this_thread::sleep_for(chrono::seconds(testDuration));
        
        // 等待所有线程完成
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    
    // 输出统计结果
    stats.printStats(testDuration);
    
    return 0;
} 