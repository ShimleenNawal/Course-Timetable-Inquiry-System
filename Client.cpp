// Client for the timetable app. Run Server first, then this program.

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>

#include "protocol.h"

using namespace std;

#define DEFAULT_PORT 50000


// Column widths for the text table of search results.
static const int COL_CODE       = 10;
static const int COL_TITLE      = 45;
static const int COL_SECTION    = 8;
static const int COL_INSTRUCTOR = 30;
static const int COL_DAY        = 12;
static const int COL_TIME       = 8;
static const int COL_CREDITS    = 8;
static const int COL_OFFERING   = 18;

static vector<string> wrapCell(const string& text, int width) {
    vector<string> lines;
    if (width <= 0) { lines.push_back(""); return lines; }

    // Break text into words; long words are cut to fit the width.
    istringstream iss(text);
    vector<string> words;
    string w;
    while (iss >> w) words.push_back(w);

    // Empty cell becomes one blank line.
    if (words.empty()) {
        lines.push_back("");
        return lines;
    }

    string cur;
    for (const auto& word : words) {
        if ((int)word.size() > width) {
            // Finish the current line before a long word.
            if (!cur.empty()) {
                lines.push_back(cur);
                cur.clear();
            }
            // Split a long word across lines.
            for (size_t i = 0; i < word.size(); i += (size_t)width) {
                lines.push_back(word.substr(i, (size_t)width));
            }
            continue;
        }

        if (cur.empty()) {
            cur = word;
        } else if ((int)cur.size() + 1 + (int)word.size() <= width) {
            cur += " " + word;
        } else {
            lines.push_back(cur);
            cur = word;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

void printDivider() {
    cout << "  +"
         << string(COL_CODE, '-') << "+"
         << string(COL_TITLE, '-') << "+"
         << string(COL_SECTION, '-') << "+"
         << string(COL_INSTRUCTOR, '-') << "+"
         << string(COL_DAY, '-') << "+"
         << string(COL_TIME, '-') << "+"
         << string(COL_CREDITS, '-') << "+"
         << string(COL_OFFERING, '-') << "+\n";
}

void printHeader() {
    printDivider();
    cout << "  |" << left << setw(COL_CODE) << "Code"
         << "|" << left << setw(COL_TITLE) << "Title"
         << "|" << left << setw(COL_SECTION) << "Sec"
         << "|" << left << setw(COL_INSTRUCTOR) << "Instructor"
         << "|" << left << setw(COL_DAY) << "Day"
         << "|" << left << setw(COL_TIME) << "Time"
         << "|" << left << setw(COL_CREDITS) << "Credits"
         << "|" << left << setw(COL_OFFERING) << "Offering"
         << "|\n";
    printDivider();
}

static bool parseResultFields(const string& line, vector<string>& outFields) {
    // Line looks like: RESULT then eight fields with | between them.
    outFields.clear();
    if (line.length() <= 7) return false;
    string data = line.substr(7);

    string temp = data;
    size_t pos;
    while ((pos = temp.find("|")) != string::npos) {
        outFields.push_back(temp.substr(0, pos));
        temp = temp.substr(pos + 1);
    }
    outFields.push_back(temp);
    return outFields.size() >= 8;
}

static void printWrappedRow(const vector<string>& fields) {
    // Wrap each column to its width.
    vector<string> codeLines       = wrapCell(fields[0], COL_CODE);
    vector<string> titleLines      = wrapCell(fields[1], COL_TITLE);
    vector<string> sectionLines    = wrapCell(fields[2], COL_SECTION);
    vector<string> instructorLines = wrapCell(fields[3], COL_INSTRUCTOR);
    vector<string> dayLines        = wrapCell(fields[4], COL_DAY);
    vector<string> timeLines       = wrapCell(fields[5], COL_TIME);
    vector<string> creditsLines    = wrapCell(fields[6], COL_CREDITS);
    vector<string> offeringLines   = wrapCell(fields[7], COL_OFFERING);

    size_t maxLines = codeLines.size();
    maxLines = max(maxLines, titleLines.size());
    maxLines = max(maxLines, sectionLines.size());
    maxLines = max(maxLines, instructorLines.size());
    maxLines = max(maxLines, dayLines.size());
    maxLines = max(maxLines, timeLines.size());
    maxLines = max(maxLines, creditsLines.size());
    maxLines = max(maxLines, offeringLines.size());

    for (size_t i = 0; i < maxLines; ++i) {
        const string& code       = (i < codeLines.size() ? codeLines[i] : "");
        const string& title      = (i < titleLines.size() ? titleLines[i] : "");
        const string& sec        = (i < sectionLines.size() ? sectionLines[i] : "");
        const string& inst       = (i < instructorLines.size() ? instructorLines[i] : "");
        const string& day        = (i < dayLines.size() ? dayLines[i] : "");
        const string& time       = (i < timeLines.size() ? timeLines[i] : "");
        const string& cred       = (i < creditsLines.size() ? creditsLines[i] : "");
        const string& unit       = (i < offeringLines.size() ? offeringLines[i] : "");

        cout << "  |" << left << setw(COL_CODE) << code
             << "|" << left << setw(COL_TITLE) << title
             << "|" << left << setw(COL_SECTION) << sec
             << "|" << left << setw(COL_INSTRUCTOR) << inst
             << "|" << left << setw(COL_DAY) << day
             << "|" << left << setw(COL_TIME) << time
             << "|" << left << setw(COL_CREDITS) << cred
             << "|" << left << setw(COL_OFFERING) << unit
             << "|\n";
    }
}


// Turn one RESULT line into a readable table row (cells can wrap).
void printResult(const string& line) {
    vector<string> fields;
    if (!parseResultFields(line, fields)) {
        if (line.length() > 7) cout << "  " << line.substr(7) << "\n";
        return;
    }
    printWrappedRow(fields);
}


// Read server replies. TCP may split one message across several recv calls,
// so we join buffers and only handle full lines ending in newline.
void receiveResponse(SOCKET sock) {
    char buffer[BUFFER_SIZE];
    string accumulated = "";
    vector<vector<string>> pendingRows;

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int len = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (len <= 0) {
            cout << "[!] Connection to server lost.\n";
            return;
        }

        buffer[len] = '\0';
        accumulated += buffer;

        size_t pos;
        while ((pos = accumulated.find('\n')) != string::npos) {
            string line = accumulated.substr(0, pos);
            accumulated.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r') line.pop_back(); // Windows line ending
            if (line.empty()) continue;

            if (line.find(RESP_RESULT) == 0) {
                vector<string> fields;
                if (parseResultFields(line, fields)) {
                    pendingRows.push_back(fields);
                } else {
                    cout << "  " << line << "\n"; // bad RESULT line, show as-is
                }
            }
            else if (line.find(RESP_END) == 0) {
                if (!pendingRows.empty()) {
                    printHeader();
                    for (const auto& row : pendingRows) {
                        printWrappedRow(row);
                    }
                    printDivider();
                }
                return;
            }
            else if (line.find(RESP_SUCCESS) == 0) {
                cout << "  [OK] Operation successful.\n";
                return;
            }
            else if (line.find("ERROR") == 0) {
                cout << "  [!!] " << line << "\n";
                return;
            }
            else if (line.find(RESP_FAILURE) == 0) {
                cout << "  [!!] " << line << "\n";
                return;
            }
            else {
                cout << "  " << line << "\n";
            }
        }
    }
}


void printStudentMenu() {
    cout << "\n";
    cout << "  +------------------------------------------+\n";
    cout << "  |     TIMETABLE INQUIRY SYSTEM             |\n";
    cout << "  +------------------------------------------+\n";
    cout << "  |  1. Search by Course Code                |\n";
    cout << "  |  2. Search by Instructor                 |\n";
    cout << "  |  3. View All Courses                     |\n";
    cout << "  |  4. Admin Login                          |\n";
    cout << "  |  5. Exit                                 |\n";
    cout << "  +------------------------------------------+\n";
    cout << "  Choice: ";
}


void printAdminMenu() {
    cout << "\n";
    cout << "  +------------------------------------------+\n";
    cout << "  |     TIMETABLE INQUIRY SYSTEM [ADMIN]     |\n";
    cout << "  +------------------------------------------+\n";
    cout << "  |  1. Search by Course Code                |\n";
    cout << "  |  2. Search by Instructor                 |\n";
    cout << "  |  3. View All Courses                     |\n";
    cout << "  |  -- Admin Functions -------------------- |\n";
    cout << "  |  4. Add New Course                       |\n";
    cout << "  |  5. Update Course Field                  |\n";
    cout << "  |  6. Delete Course                        |\n";
    cout << "  |  7. Logout                               |\n";
    cout << "  |  8. Exit                                 |\n";
    cout << "  +------------------------------------------+\n";
    cout << "  Choice: ";
}


// Send a command; newline marks the end of one command for the server.
void sendCommand(SOCKET sock, const string& cmd) {
    string full = cmd + "\n";
    int total = 0;
    int len = (int)full.length();
    const char* data = full.c_str();
    while (total < len) {
        int sent = send(sock, data + total, len - total, 0);
        if (sent == SOCKET_ERROR) {
            cout << "[!] Send error.\n";
            return;
        }
        total += sent;
    }
}


int main() {
    WSADATA wsa;
    SOCKET sock;
    struct sockaddr_in server;
    bool isAdmin = false;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cout << "WSAStartup failed.\n";
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cout << "Socket creation failed.\n";
        return 1;
    }

    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_family      = AF_INET;
    server.sin_port        = htons(DEFAULT_PORT);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        cout << "[!] Connection failed. Make sure the server is running!\n";
        WSACleanup();
        return 1;
    }

    cout << "\n  Connected to Timetable Server on port " << DEFAULT_PORT << "\n";
    sendCommand(sock, string(CMD_HELLO) + " CLI");

    while (true) {
        if (isAdmin) {
            printAdminMenu();
        } else {
            printStudentMenu();
        }

        string choice;
        getline(cin, choice);
        cout << "\n";

        if (!isAdmin) {
            if (choice == "1") {
                cout << "  Enter course code: ";
                string code; getline(cin, code);
                sendCommand(sock, string(CMD_QUERY) + " " + code);
                receiveResponse(sock);
            }
            else if (choice == "2") {
                cout << "  Enter instructor name: ";
                string name; getline(cin, name);
                sendCommand(sock, string(CMD_SEARCH_INST) + " " + name);
                receiveResponse(sock);
            }
            else if (choice == "3") {
                sendCommand(sock, CMD_VIEW_ALL);
                receiveResponse(sock);
            }
            else if (choice == "4") {
                cout << "  Username: ";
                string user; getline(cin, user);
                cout << "  Password: ";
                string pass; getline(cin, pass);
                sendCommand(sock, string(CMD_LOGIN) + " " + user + " " + pass);

                char buf[BUFFER_SIZE];
                memset(buf, 0, sizeof(buf));
                int len = recv(sock, buf, sizeof(buf) - 1, 0);
                if (len > 0) {
                    string resp(buf);
                    if (resp.find(RESP_SUCCESS) != string::npos) {
                        isAdmin = true;
                        cout << "  [OK] Admin login successful!\n";
                    } else {
                        cout << "  [!!] " << resp;
                    }
                }
            }
            else if (choice == "5" || choice == "exit") {
                break;
            }
            else {
                cout << "  [!] Invalid choice. Please enter 1-5.\n";
            }
        }

        else {
            if (choice == "1") {
                cout << "  Enter course code: ";
                string code; getline(cin, code);
                sendCommand(sock, string(CMD_QUERY) + " " + code);
                receiveResponse(sock);
            }
            else if (choice == "2") {
                cout << "  Enter instructor name: ";
                string name; getline(cin, name);
                sendCommand(sock, string(CMD_SEARCH_INST) + " " + name);
                receiveResponse(sock);
            }
            else if (choice == "3") {
                sendCommand(sock, CMD_VIEW_ALL);
                receiveResponse(sock);
            }
            else if (choice == "4") {
                cout << "  Enter details (each field separately):\n";
                string code, title, section, instructor, day, time, credits, offeringUnit;
                cout << "  Course Code:  "; getline(cin, code);
                cout << "  Title:        "; getline(cin, title);
                cout << "  Section:      "; getline(cin, section);
                cout << "  Instructor:   "; getline(cin, instructor);
                cout << "  Day (e.g. Monday): "; getline(cin, day);
                cout << "  Time (HH:MM): "; getline(cin, time);
                cout << "  Credits (e.g. 3): "; getline(cin, credits);
                cout << "  Offering unit: "; getline(cin, offeringUnit);

                string cmd = string(CMD_ADD) + " " +
                             code + "|" + title + "|" + section + "|" +
                             instructor + "|" + day + "|" + time + "|" +
                             credits + "|" + offeringUnit;
                sendCommand(sock, cmd);
                receiveResponse(sock);
            }
            else if (choice == "5") {
                cout << "  Course Code:  ";
                string code; getline(cin, code);
                cout << "  Field to update\n";
                cout << "  (TITLE / SECTION / INSTRUCTOR / DAY / TIME / CREDITS / OFFERING_UNIT): ";
                string field; getline(cin, field);
                cout << "  New Value:    ";
                string value; getline(cin, value);

                sendCommand(sock, string(CMD_UPDATE) + " " + code + " " + field + " " + value);
                receiveResponse(sock);
            }
            else if (choice == "6") {
                cout << "  Enter course code to delete: ";
                string code; getline(cin, code);
                cout << "  Are you sure? (yes/no): ";
                string confirm; getline(cin, confirm);
                if (confirm == "yes") {
                    sendCommand(sock, string(CMD_DELETE) + " " + code);
                    receiveResponse(sock);
                } else {
                    cout << "  Cancelled.\n";
                }
            }
            else if (choice == "7") {
                sendCommand(sock, CMD_LOGOUT);
                receiveResponse(sock);
                isAdmin = false;
                cout << "  Logged out. Back to student mode.\n";
            }
            else if (choice == "8" || choice == "exit") {
                break;
            }
            else {
                cout << "  [!] Invalid choice. Please enter 1-8.\n";
            }
        }
    }

    closesocket(sock);
    WSACleanup();
    cout << "\n  Disconnected. Goodbye!\n";
    return 0;
}
