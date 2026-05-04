// Protocol.h
// Shared communication protocol constants for the Timetable Inquiry System
// ALL team members must include this file. NEVER hardcode these strings elsewhere.

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>

// ============================================================
// BUFFER SIZE
// ============================================================
#define BUFFER_SIZE 1024

// ============================================================
// CLIENT → SERVER COMMANDS
// ============================================================

// Student commands
#define CMD_QUERY           "QUERY"             // QUERY <course_code>
#define CMD_SEARCH_INST     "SEARCH_INSTRUCTOR" // SEARCH_INSTRUCTOR <name>
#define CMD_VIEW_ALL        "VIEW_ALL"          // VIEW_ALL

// Admin commands (must be logged in first)
#define CMD_LOGIN           "LOGIN"             // LOGIN <username> <password>
#define CMD_LOGOUT          "LOGOUT"            // LOGOUT
#define CMD_ADD             "ADD"               // ADD <code> <title> <section> <instructor> <day> <time> <duration> <classroom>
#define CMD_UPDATE          "UPDATE"            // UPDATE <course_code> <field> <new_value>
#define CMD_DELETE          "DELETE"            // DELETE <course_code>

// ============================================================
// SERVER → CLIENT RESPONSES
// ============================================================
#define RESP_RESULT         "RESULT"            // RESULT <data>
#define RESP_SUCCESS        "SUCCESS"           // SUCCESS
#define RESP_FAILURE        "FAILURE"           // FAILURE
#define RESP_ERROR          "ERROR"             // ERROR <message>
#define RESP_END            "END"               // END (signals end of multi-line results)

// ============================================================
// UPDATE FIELDS (used with CMD_UPDATE)
// ============================================================
#define FIELD_TITLE         "TITLE"
#define FIELD_SECTION       "SECTION"
#define FIELD_INSTRUCTOR    "INSTRUCTOR"
#define FIELD_DAY           "DAY"
#define FIELD_TIME          "TIME"
#define FIELD_DURATION      "DURATION"
#define FIELD_CLASSROOM     "CLASSROOM"

// ============================================================
// ERROR MESSAGES (server sends these to client)
// ============================================================
#define ERR_NOT_FOUND       "ERROR Course not found"
#define ERR_NOT_AUTHORIZED  "ERROR Not authorized - please login as admin"
#define ERR_INVALID_CMD     "ERROR Invalid command"
#define ERR_INVALID_FORMAT  "ERROR Invalid request format"
#define ERR_ALREADY_EXISTS  "ERROR Course already exists"
#define ERR_DB_FAIL         "ERROR Database operation failed"
#define ERR_WRONG_PASSWORD  "ERROR Wrong username or password"

// ============================================================
// ADMIN CREDENTIALS (Member 4 can move this to Auth.cpp later)
// ============================================================
#define ADMIN_USERNAME      "admin"
#define ADMIN_PASSWORD      "password123"

// ============================================================
// SEPARATOR (used to separate fields in messages)
// ============================================================
#define DELIMITER           "|"  // e.g. "COMP3003|Networks|A|Dr.Smith|Monday|10:00|90min|Room301"

#endif // PROTOCOL_H