// Shared text commands and replies between client and server. Use these names, do not spell strings by hand.

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>

#define BUFFER_SIZE 1024

#define CMD_HELLO           "HELLO"
#define CMD_QUERY           "QUERY"
#define CMD_SEARCH_INST     "SEARCH_INSTRUCTOR"
#define CMD_VIEW_ALL        "VIEW_ALL"

#define CMD_LOGIN           "LOGIN"
#define CMD_LOGOUT          "LOGOUT"
#define CMD_ADD             "ADD"
#define CMD_UPDATE          "UPDATE"
#define CMD_DELETE          "DELETE"

#define RESP_RESULT         "RESULT"
#define RESP_SUCCESS        "SUCCESS"
#define RESP_FAILURE        "FAILURE"
#define RESP_ERROR          "ERROR"
#define RESP_END            "END"

#define FIELD_TITLE         "TITLE"
#define FIELD_SECTION       "SECTION"
#define FIELD_INSTRUCTOR    "INSTRUCTOR"
#define FIELD_DAY           "DAY"
#define FIELD_TIME          "TIME"
#define FIELD_CREDITS        "CREDITS"
#define FIELD_OFFERING_UNIT  "OFFERING_UNIT"

#define ERR_NOT_FOUND       "ERROR Course not found"
#define ERR_NOT_AUTHORIZED  "ERROR Not authorized - please login as admin"
#define ERR_INVALID_CMD     "ERROR Invalid command"
#define ERR_INVALID_FORMAT  "ERROR Invalid request format"
#define ERR_ALREADY_EXISTS  "ERROR Course already exists"
#define ERR_DB_FAIL         "ERROR Database operation failed"
#define ERR_WRONG_PASSWORD  "ERROR Wrong username or password"

#define ADMIN_USERNAME      "admin"
#define ADMIN_PASSWORD      "password123"

#define DELIMITER           "|"

#endif // PROTOCOL_H
