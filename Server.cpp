

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include <ctime>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "protocol.h"
#include "database.h"

using namespace std;

#define DEFAULT_PORT 50000
#define SERVER_LOG   "server.log"


// Log one line with a time stamp to the screen and server.log.
void serverLog(const string& msg) {
    time_t t = time(nullptr);
    char timebuf[32];
    struct tm tmInfo;
    localtime_s(&tmInfo, &t);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

    string line = "[" + string(timebuf) + "] " + msg;

    printf("%s\n", line.c_str());

    ofstream logFile(SERVER_LOG, ios::app);
    if (logFile.is_open()) {
        logFile << line << "\n";
    }
}


// Cut off newline and spaces at the end (the client always ends lines with newline).
string stripNewlines(const string& s) {
    string result = s;
    while (!result.empty() &&
           (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
        result.pop_back();
    return result;
}


// IP and port as text for logs (port matters when many clients use 127.0.0.1).
string makeEndpoint(const sockaddr_in& client_addr) {
    return string(inet_ntoa(client_addr.sin_addr)) + ":" +
           to_string(ntohs(client_addr.sin_port));
}


// Send one line; add newline so the client can read line by line.
void sendMsg(SOCKET sock, const string& msg) {
    string full = msg + "\n";
    send(sock, full.c_str(), (int)full.length(), 0);
}


// Split a multi-line string from the DB into separate lines.
vector<string> splitLines(const string& s) {
    vector<string> lines;
    istringstream stream(s);
    string line;
    while (getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}


// Send each course as RESULT ..., then END when finished.
void sendResults(SOCKET sock, const string& dbResult) {
    if (dbResult.empty()) {
        sendMsg(sock, ERR_NOT_FOUND);
        return;
    }
    vector<string> lines = splitLines(dbResult);
    for (const string& line : lines) {
        sendMsg(sock, string(RESP_RESULT) + " " + line);
        Sleep(10); // short wait so lines are less likely to arrive in one chunk
    }
    sendMsg(sock, RESP_END);
}


// LOGIN username password
void handleLogin(const string& msg, bool& isAdmin,
                 SOCKET sock, const string& clientIP) {
    if (msg.length() <= 6) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string rest = msg.substr(6); // after "LOGIN "
    size_t space = rest.find(' ');
    if (space == string::npos) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string username = stripNewlines(rest.substr(0, space));
    string password = stripNewlines(rest.substr(space + 1));

    if (username == ADMIN_USERNAME && password == ADMIN_PASSWORD) {
        isAdmin = true;
        sendMsg(sock, RESP_SUCCESS);
        serverLog("[AUTH] Admin login SUCCESS from " + clientIP);
    } else {
        sendMsg(sock, ERR_WRONG_PASSWORD);
        serverLog("[AUTH] Login FAILED from " + clientIP + " (bad credentials)");
    }
}


// QUERY course_code
void handleQuery(const string& msg, SOCKET sock, const string& clientIP) {
    if (msg.length() <= 6) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string code = msg.substr(6); // after "QUERY "
    serverLog("[QUERY] Code=" + code + " from " + clientIP);
    sendResults(sock, searchByCode(code));
}


// SEARCH_INSTRUCTOR name (prefix is 18 characters)
void handleSearchInstructor(const string& msg, SOCKET sock, const string& clientIP) {
    if (msg.length() <= 18) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string name = msg.substr(18);
    serverLog("[QUERY] Instructor=" + name + " from " + clientIP);
    sendResults(sock, searchByInstructor(name));
}


// VIEW_ALL
void handleViewAll(SOCKET sock, const string& clientIP) {
    serverLog("[QUERY] VIEW_ALL from " + clientIP);
    sendResults(sock, viewAll());
}


// ADD with eight fields separated by | (admin only).
void handleAdd(const string& msg, SOCKET sock, const string& clientIP) {
    if (msg.length() <= 4) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string data = msg.substr(4); // after "ADD "

    vector<string> fields;
    string temp = data;
    size_t pos;
    while ((pos = temp.find("|")) != string::npos) {
        fields.push_back(temp.substr(0, pos));
        temp = temp.substr(pos + 1);
    }
    fields.push_back(temp);

    if (fields.size() < 8) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        serverLog("[ADD] FAILED - only " + to_string(fields.size()) + " fields from " + clientIP);
        return;
    }

    bool ok = addCourse(fields[0], fields[1], fields[2], fields[3],
                        fields[4], fields[5], fields[6], fields[7]);

    sendMsg(sock, ok ? RESP_SUCCESS : ERR_DB_FAIL);
    serverLog("[ADD] " + fields[0] + " from " + clientIP + ": " + (ok ? "SUCCESS" : "FAILED"));
}


// UPDATE code field value (admin only).
void handleUpdate(const string& msg, SOCKET sock, const string& clientIP) {
    if (msg.length() <= 7) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string rest = msg.substr(7); // after "UPDATE "

    size_t p1 = rest.find(' ');
    if (p1 == string::npos) { sendMsg(sock, ERR_INVALID_FORMAT); return; }
    string code = stripNewlines(rest.substr(0, p1));

    string rest2 = rest.substr(p1 + 1);
    size_t p2 = rest2.find(' ');
    if (p2 == string::npos) { sendMsg(sock, ERR_INVALID_FORMAT); return; }
    string field = stripNewlines(rest2.substr(0, p2));
    string value = stripNewlines(rest2.substr(p2 + 1));

    if (searchByCode(code).empty()) {
        sendMsg(sock, ERR_NOT_FOUND);
        serverLog("[UPDATE] " + code + " from " + clientIP + ": NOT_FOUND");
        return;
    }

    bool ok = updateCourse(code, field, value);
    sendMsg(sock, ok ? RESP_SUCCESS : ERR_DB_FAIL);
    serverLog("[UPDATE] " + code + " " + field + "=" + value +
              " from " + clientIP + ": " + (ok ? "SUCCESS" : "FAILED"));
}


// DELETE code (admin only).
void handleDelete(const string& msg, SOCKET sock, const string& clientIP) {
    if (msg.length() <= 7) {
        sendMsg(sock, ERR_INVALID_FORMAT);
        return;
    }
    string code = stripNewlines(msg.substr(7)); // after "DELETE "

    if (searchByCode(code).empty()) {
        sendMsg(sock, ERR_NOT_FOUND);
        serverLog("[DELETE] " + code + " from " + clientIP + ": NOT_FOUND");
        return;
    }

    bool ok = deleteCourse(code);
    sendMsg(sock, ok ? RESP_SUCCESS : ERR_DB_FAIL);
    serverLog("[DELETE] " + code + " from " + clientIP + ": " + (ok ? "SUCCESS" : "FAILED"));
}


// One thread per connected client.
void handleClient(SOCKET client_sock, sockaddr_in client_addr) {
    char buffer[BUFFER_SIZE];
    bool isAdmin = false;
    string endpoint = makeEndpoint(client_addr);
    string clientID = "Unknown@" + endpoint;

    serverLog("[CONNECT] Client connected: " + clientID);

    while (true) {
        memset(buffer, 0, sizeof(buffer));

        int msg_len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);

        if (msg_len <= 0) {
            serverLog("[DISCONNECT] Client disconnected: " + clientID);
            break;
        }

        buffer[msg_len] = '\0';

        string msg = stripNewlines(string(buffer));

        if (msg.empty()) continue;

        serverLog("[RECV] From " + clientID + ": " + msg);

        if (msg.find(CMD_HELLO) == 0) {
            string name = stripNewlines(msg.length() > 6 ? msg.substr(6) : "");
            if (name.empty()) name = "Unknown";
            clientID = name + "@" + endpoint;
            serverLog("[CLIENT] Identified as " + clientID);
        }
        else if (msg.find(CMD_LOGIN) == 0) {
            handleLogin(msg, isAdmin, client_sock, clientID);
        }
        else if (msg.find(CMD_LOGOUT) == 0) {
            isAdmin = false;
            sendMsg(client_sock, RESP_SUCCESS);
            serverLog("[AUTH] Client logged out: " + clientID);
        }
        else if (msg.find(CMD_QUERY) == 0) {
            handleQuery(msg, client_sock, clientID);
        }
        else if (msg.find(CMD_SEARCH_INST) == 0) {
            handleSearchInstructor(msg, client_sock, clientID);
        }
        else if (msg.find(CMD_VIEW_ALL) == 0) {
            handleViewAll(client_sock, clientID);
        }
        else if (msg.find(CMD_ADD) == 0) {
            if (!isAdmin) {
                sendMsg(client_sock, ERR_NOT_AUTHORIZED);
                serverLog("[AUTH] Unauthorized ADD from " + clientID);
            }
            else handleAdd(msg, client_sock, clientID);
        }
        else if (msg.find(CMD_UPDATE) == 0) {
            if (!isAdmin) {
                sendMsg(client_sock, ERR_NOT_AUTHORIZED);
                serverLog("[AUTH] Unauthorized UPDATE from " + clientID);
            }
            else handleUpdate(msg, client_sock, clientID);
        }
        else if (msg.find(CMD_DELETE) == 0) {
            if (!isAdmin) {
                sendMsg(client_sock, ERR_NOT_AUTHORIZED);
                serverLog("[AUTH] Unauthorized DELETE from " + clientID);
            }
            else handleDelete(msg, client_sock, clientID);
        }
        else {
            sendMsg(client_sock, ERR_INVALID_CMD);
            serverLog("[WARN] Unknown command from " + clientID + ": " + msg);
        }
    }

    closesocket(client_sock);
}


int main() {
    WSADATA wsaData;
    SOCKET sock, msg_sock;
    struct sockaddr_in local, client_addr;
    int addr_len = sizeof(client_addr);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    local.sin_family      = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port        = htons(DEFAULT_PORT);

    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    if (listen(sock, 10) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    loadCourses();
    {
        string dbPath = databaseResolvedPath(DEFAULT_DB_FILE);
        const int n = getCourseCount();
        serverLog("[STARTUP] Database file: " + dbPath);
        serverLog("[STARTUP] Course rows in memory: " + to_string(n));
        printf("  Database file: %s\n", dbPath.c_str());
        printf("  Course rows loaded: %d\n", n);
    }

    serverLog("[STARTUP] Timetable Server started on port " + to_string(DEFAULT_PORT));
    printf("================================================\n");
    printf("  Timetable Server running on port %d\n", DEFAULT_PORT);
    printf("  Logs: server.log\n");
    printf("  Waiting for connections...\n");
    printf("================================================\n");

    while (true) {
        msg_sock = accept(sock, (struct sockaddr*)&client_addr, &addr_len);
        if (msg_sock == INVALID_SOCKET) {
            serverLog("[ERROR] accept() failed: " + to_string(WSAGetLastError()));
            continue;
        }
        thread t(handleClient, msg_sock, client_addr);
        t.detach();
    }

    shutdownDatabase();
    closesocket(sock);
    WSACleanup();
    return 0;
}
