// Timetable data: CSV file on disk, copy in memory, safe for many threads at once.

#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <mutex>
#include <ctime>

#define DEFAULT_DB_FILE     "courses_bnbu.csv"
// Optional: full path to the CSV if you do not want the default search order.
#define DB_LOG_FILE         "database.log"
#define MAX_DB_FILE_SIZE    (10 * 1024 * 1024)  // do not load files bigger than this
#define CSV_FIELD_COUNT     8

struct Course {
    std::string code;
    std::string title;
    std::string section;
    std::string instructor;
    std::string day;
    std::string time;
    std::string credits;
    std::string offeringUnit;

    std::string toProtocolString() const;
    std::string toCSVRow() const;
    static bool fromCSVRow(const std::string& row, Course& out);
};

std::vector<Course> loadCourses(const std::string& filename = DEFAULT_DB_FILE);
bool saveCourses(const std::vector<Course>& courses,
                 const std::string& filename = DEFAULT_DB_FILE);

// Search: returns text with | between fields; several courses separated by newline.
// Empty string means nothing matched.
std::string searchByCode(const std::string& code,
                         const std::string& filename = DEFAULT_DB_FILE);

std::string searchByInstructor(const std::string& name,
                               const std::string& filename = DEFAULT_DB_FILE);

std::string viewAll(const std::string& filename = DEFAULT_DB_FILE);

// Path the module will actually open (current folder, next to the exe, or env override).
std::string databaseResolvedPath(const std::string& filename = DEFAULT_DB_FILE);

bool addCourse(const std::string& code,
               const std::string& title,
               const std::string& section,
               const std::string& instructor,
               const std::string& day,
               const std::string& time,
               const std::string& credits,
               const std::string& offeringUnit,
               const std::string& filename = DEFAULT_DB_FILE);

bool updateCourse(const std::string& code,
                  const std::string& field,
                  const std::string& newValue,
                  const std::string& filename = DEFAULT_DB_FILE);

bool deleteCourse(const std::string& code,
                  const std::string& filename = DEFAULT_DB_FILE);

std::string searchByTimeSlot(const std::string& day,
                             const std::string& time,
                             const std::string& filename = DEFAULT_DB_FILE);

std::string searchByOfferingUnit(const std::string& offeringUnit,
                                  const std::string& filename = DEFAULT_DB_FILE);

int getCourseCount(const std::string& filename = DEFAULT_DB_FILE);

std::vector<std::string> getUniqueInstructors(
        const std::string& filename = DEFAULT_DB_FILE);

bool exportToJSON(const std::string& outPath,
                  const std::string& filename = DEFAULT_DB_FILE);

bool backupDatabase(const std::string& filename = DEFAULT_DB_FILE);

bool isValidCourseCode(const std::string& code);
bool isValidTime(const std::string& time);
bool isValidDay(const std::string& day);
bool isValidCredits(const std::string& credits);
bool isValidCourse(const Course& c, std::string& errOut);

void invalidateCache();
void shutdownDatabase();

#endif // DATABASE_H
