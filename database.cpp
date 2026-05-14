#include "database.h"
#include "protocol.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
    std::mutex g_dbMutex;
    std::mutex g_logMutex;

    std::vector<Course> g_cache;
    bool   g_cacheValid   = false;
    bool   g_cacheDirty   = false;
    time_t g_cacheMTime   = 0;
    std::string g_cachedFile;

    std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return s;
    }

    std::string toUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return std::toupper(c); });
        return s;
    }

    std::string nowTimestamp() {
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream os;
        os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return os.str();
    }

    std::string timestampCompact() {
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream os;
        os << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return os.str();
    }

    void dbLog(const std::string& level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(g_logMutex);
        std::string line = "[" + nowTimestamp() + "] [" + level + "] " + msg;

        std::ofstream f(DB_LOG_FILE, std::ios::app);
        if (f.is_open()) {
            f << line << "\n";
        }
    }

    time_t fileMTime(const std::string& path) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) return 0;
        return st.st_mtime;
    }

    long long fileSize(const std::string& path) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) return -1;
        return static_cast<long long>(st.st_size);
    }

    bool fileExists(const std::string& path) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0;
    }

    bool isAbsoluteDbPath(const std::string& p) {
        if (p.empty()) return false;
#ifdef _WIN32
        if (p.size() >= 2) {
            unsigned char c0 = static_cast<unsigned char>(p[0]);
            if (std::isalpha(c0) != 0 && p[1] == ':') return true;
        }
        if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\') return true;
#endif
        return p[0] == '/' || p[0] == '\\';
    }

    std::string executableDirectory() {
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return "";
        std::string full(buf, buf + n);
        size_t pos = full.find_last_of("\\/");
        if (pos == std::string::npos) return "";
        return full.substr(0, pos + 1);
#else
        (void)0;
        return "";
#endif
    }

    // Pick where the CSV lives: env path if set and file exists, else current folder, else next to the exe.
    std::string resolveDatabaseFile(const std::string& filename) {
        if (filename.empty() || isAbsoluteDbPath(filename))
            return filename;

        const char* envPath = std::getenv("DATACOM_DB_FILE");
        if (envPath && envPath[0]) {
            std::string p(envPath);
            if (fileExists(p))
                return p;
        }

        const std::string exedir = executableDirectory();
        const std::string nextToExe = exedir.empty() ? std::string() : exedir + filename;

        if (fileExists(filename))
            return filename;
        if (!nextToExe.empty() && fileExists(nextToExe))
            return nextToExe;
        return filename;
    }

    // Quote CSV fields that contain comma, quote, or newline.
    std::string csvEscape(const std::string& s) {
        bool needQuote = s.find_first_of(",\"\n\r") != std::string::npos;
        if (!needQuote) return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += "\"\"";
            else out += c;
        }
        out += "\"";
        return out;
    }

    // Split one CSV line into fields; handles "quoted, bits".
    std::vector<std::string> csvSplit(const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        bool inQuotes = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        cur += '"'; ++i;
                    } else {
                        inQuotes = false;
                    }
                } else cur += c;
            } else {
                if (c == ',')      { out.push_back(cur); cur.clear(); }
                else if (c == '"') { inQuotes = true; }
                else               { cur += c; }
            }
        }
        out.push_back(cur);
        return out;
    }

    // Try to read one row from the wide BN-BU export (many columns). Schedule like "Fri 13:00-14:50".
    bool tryFromBnbuExportRow(const std::vector<std::string>& fields, Course& out) {
        if (fields.size() < 10) return false;

        const std::string offeringUnit = trim(fields[0]);
        const std::string code = trim(fields[2]);
        static const std::regex reCode("^[A-Z]{2,8}[0-9]{2,6}$");
        if (!std::regex_match(code, reCode)) return false;

        std::string title = trim(fields[3]);
        const std::string creditsStr = trim(fields[4]);
        const std::string instructor = trim(fields[5]);
        const std::string schedule = trim(fields[6]);

        if (offeringUnit.empty() || title.empty() || instructor.empty() ||
            schedule.empty() || creditsStr.empty())
            return false;
        if (!isValidCredits(creditsStr)) return false;

        std::string section;
        {
            static const std::regex reSec(R"(\(([0-9]{3,5})\)\s*$)");
            std::smatch sm;
            if (std::regex_search(title, sm, reSec))
                section = sm[1].str();
            else
                section = "000";
        }

        static const std::regex reSched(
            R"(^(Mon|Tue|Wed|Thu|Fri)\s+(\d{1,2}):(\d{2})\s*-\s*(\d{1,2}):(\d{2})$)");
        std::smatch m;
        if (!std::regex_match(schedule, m, reSched)) return false;

        const std::string& ab = m[1].str();
        int h1 = std::stoi(m[2].str());
        int mi1 = std::stoi(m[3].str());
        if (h1 < 0 || h1 > 23 || mi1 < 0 || mi1 > 59) return false;

        std::string day;
        if      (ab == "Mon") day = "Monday";
        else if (ab == "Tue") day = "Tuesday";
        else if (ab == "Wed") day = "Wednesday";
        else if (ab == "Thu") day = "Thursday";
        else if (ab == "Fri") day = "Friday";
        else return false;

        std::ostringstream tss;
        tss << std::setfill('0') << std::setw(2) << h1 << ':'
            << std::setw(2) << mi1;
        const std::string timeStart = tss.str();

        out.code = code;
        out.title = title;
        out.section = section;
        out.instructor = instructor;
        out.day = day;
        out.time = timeStart;
        out.credits = creditsStr;
        out.offeringUnit = offeringUnit;
        return true;
    }

    std::string courseRowDedupKey(const Course& c) {
        return toUpper(c.code) + "|" + c.section + "|" + c.day + "|" + c.time;
    }

    // Load cache from disk if needed. Caller must already hold g_dbMutex.
    void ensureCacheLoaded_locked(const std::string& filename) {
        const std::string path = resolveDatabaseFile(filename);

        time_t currentMTime = fileMTime(path);
        bool fileChanged = (currentMTime != g_cacheMTime);
        bool wrongFile   = (g_cachedFile != path);

        if (g_cacheValid && !fileChanged && !wrongFile) {
            dbLog("DEBUG", "Cache HIT for " + path);
            return;
        }

        dbLog("DEBUG", "Cache MISS for " + path +
              (wrongFile ? " (file switched)" :
               fileChanged ? " (file modified)" : " (first load)"));

        // Read file into loaded (bypass public helpers to avoid locking again).
        std::vector<Course> loaded;

        if (!fileExists(path)) {
            dbLog("WARN", "Database file missing - creating empty: " + path);
            std::ofstream create(path);
            if (create.is_open()) {
                create << "# code,title,section,instructor,day,time,credits,offering_unit\n";
            } else {
                dbLog("ERROR", "Could not create DB file: " + path);
            }
        } else {
            long long sz = fileSize(path);
            if (sz > MAX_DB_FILE_SIZE) {
                dbLog("ERROR", "DB file exceeds max size (" +
                      std::to_string(sz) + " bytes) - refusing to load.");
                g_cache.clear();
                g_cacheValid = true;
                g_cacheDirty = false;
                g_cacheMTime = currentMTime;
                g_cachedFile = path;
                return;
            }

            std::ifstream f(path);
            if (!f.is_open()) {
                dbLog("ERROR", "Failed to open DB file for read: " + path);
                g_cache.clear();
                g_cacheValid = true;
                g_cacheDirty = false;
                g_cacheMTime = currentMTime;
                g_cachedFile = path;
                return;
            }

            std::set<std::string> seenCodes;
            std::string line;
            int lineNo = 0, ok = 0, bad = 0, dup = 0;

            while (std::getline(f, line)) {
                ++lineNo;
                std::string t = trim(line);
                if (t.empty() || t[0] == '#') continue;

                Course c;
                if (!Course::fromCSVRow(t, c)) {
                    dbLog("WARN", "Corrupt CSV line " + std::to_string(lineNo) +
                          " skipped: " + t);
                    ++bad;
                    continue;
                }

                std::string errMsg;
                if (!isValidCourse(c, errMsg)) {
                    dbLog("WARN", "Invalid course on line " +
                          std::to_string(lineNo) + " (" + errMsg + ") skipped.");
                    ++bad;
                    continue;
                }

                const std::string key = courseRowDedupKey(c);
                if (seenCodes.count(key)) {
                    dbLog("WARN", "Duplicate course row " + key +
                          " on line " + std::to_string(lineNo) + " - keeping first.");
                    ++dup;
                    continue;
                }
                seenCodes.insert(key);
                loaded.push_back(c);
                ++ok;
            }

            dbLog("INFO", "Loaded " + std::to_string(ok) + " courses from " +
                  path + " (bad=" + std::to_string(bad) +
                  ", duplicates=" + std::to_string(dup) + ")");
        }

        g_cache      = std::move(loaded);
        g_cacheValid = true;
        g_cacheDirty = false;
        g_cacheMTime = fileMTime(path);
        g_cachedFile = path;
    }

    // Caller must already hold g_dbMutex.
    bool flushCache_locked(const std::string& filename) {
        if (!g_cacheDirty) return true;

        const std::string path = resolveDatabaseFile(filename);

        std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f.is_open()) {
                dbLog("ERROR", "Cannot open temp file for write: " + tmp);
                return false;
            }
            f << "# code,title,section,instructor,day,time,credits,offering_unit\n";
            for (const auto& c : g_cache) f << c.toCSVRow() << "\n";
            if (!f.good()) {
                dbLog("ERROR", "Write failed (disk full / permission?) " + tmp);
                return false;
            }
        }

        // Write temp file, then rename so we never leave a half-written main file.
        std::remove(path.c_str());
        if (std::rename(tmp.c_str(), path.c_str()) != 0) {
            dbLog("ERROR", "Rename failed - data may be in " + tmp);
            return false;
        }

        g_cacheDirty = false;
        g_cacheMTime = fileMTime(path);
        dbLog("INFO", "Flushed " + std::to_string(g_cache.size()) +
              " courses to " + path);
        return true;
    }
} // anonymous namespace

std::string Course::toProtocolString() const {
    return code + DELIMITER + title + DELIMITER + section + DELIMITER +
           instructor + DELIMITER + day + DELIMITER + time + DELIMITER +
           credits + DELIMITER + offeringUnit;
}

std::string Course::toCSVRow() const {
    std::ostringstream os;
    os << csvEscape(code)       << ","
       << csvEscape(title)      << ","
       << csvEscape(section)    << ","
       << csvEscape(instructor) << ","
       << csvEscape(day)        << ","
       << csvEscape(time)       << ","
       << csvEscape(credits)    << ","
       << csvEscape(offeringUnit);
    return os.str();
}

bool Course::fromCSVRow(const std::string& row, Course& out) {
    auto fields = csvSplit(row);
    if (fields.size() >= 10) {
        if (tryFromBnbuExportRow(fields, out)) return true;
        const std::string maybeCode = trim(fields[2]);
        static const std::regex reWide("^[A-Z]{2,8}[0-9]{2,6}$");
        if (std::regex_match(maybeCode, reWide)) return false;
    }

    if (fields.size() < CSV_FIELD_COUNT) return false;

    out.code       = trim(fields[0]);
    out.title      = trim(fields[1]);
    out.section    = trim(fields[2]);
    out.instructor = trim(fields[3]);
    out.day        = trim(fields[4]);
    out.time       = trim(fields[5]);
    out.credits       = trim(fields[6]);
    out.offeringUnit  = trim(fields[7]);

    if (out.code.empty() || out.title.empty() || out.section.empty() ||
        out.instructor.empty() || out.day.empty() || out.time.empty() ||
        out.credits.empty() || out.offeringUnit.empty()) return false;

    return true;
}

bool isValidCourseCode(const std::string& code) {
    // Letters then digits, e.g. COMP3003
    static const std::regex re("^[A-Z]{2,8}[0-9]{2,6}$");
    if (code.size() < 5 || code.size() > 14) return false;
    return std::regex_match(code, re);
}

bool isValidTime(const std::string& t) {
    static const std::regex re("^([01][0-9]|2[0-3]):[0-5][0-9]$");
    return std::regex_match(t, re);
}

bool isValidDay(const std::string& d) {
    static const std::set<std::string> ok{
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday"
    };
    return ok.count(d) > 0;
}

bool isValidCredits(const std::string& s) {
    static const std::regex re(R"(^\d+(\.\d+)?$)");
    if (!std::regex_match(s, re)) return false;
    try {
        double v = std::stod(s);
        return v > 0.0 && v <= 99.0;
    } catch (...) {
        return false;
    }
}

bool isValidCourse(const Course& c, std::string& errOut) {
    if (!isValidCourseCode(c.code)) { errOut = "bad code: " + c.code; return false; }
    if (c.title.empty())            { errOut = "empty title";          return false; }
    if (c.section.empty())          { errOut = "empty section";        return false; }
    if (c.instructor.empty())       { errOut = "empty instructor";     return false; }
    if (!isValidDay(c.day))         { errOut = "bad day: " + c.day;    return false; }
    if (!isValidTime(c.time))       { errOut = "bad time: " + c.time;  return false; }
    if (!isValidCredits(c.credits)) { errOut = "bad credits: " + c.credits; return false; }
    if (c.offeringUnit.empty())     { errOut = "empty offering unit";  return false; }
    return true;
}

std::vector<Course> loadCourses(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);
    return g_cache;
}

std::string databaseResolvedPath(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    return resolveDatabaseFile(filename);
}

bool saveCourses(const std::vector<Course>& courses,
                 const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    const std::string path = resolveDatabaseFile(filename);
    g_cache      = courses;
    g_cacheValid = true;
    g_cacheDirty = true;
    g_cachedFile = path;
    bool ok = flushCache_locked(path);
    dbLog(ok ? "INFO" : "ERROR",
          std::string("saveCourses: ") + (ok ? "SUCCESS" : "FAILED") +
          " - rows=" + std::to_string(courses.size()));
    return ok;
}

std::string searchByCode(const std::string& code, const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::string key = toUpper(trim(code));
    std::ostringstream os;
    int count = 0;
    for (const auto& c : g_cache) {
        if (toUpper(c.code) == key) {
            if (count++) os << "\n";
            os << c.toProtocolString();
        }
    }
    dbLog("INFO", "searchByCode(" + code + "): " +
          (count ? std::to_string(count) + " result(s)" : "MISS"));
    return os.str();
}

std::string searchByInstructor(const std::string& name,
                               const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::string needle = toLower(trim(name));
    std::ostringstream os;
    int count = 0;
    for (const auto& c : g_cache) {
        // Match instructor name ignoring case; substring is ok.
        if (toLower(c.instructor).find(needle) != std::string::npos) {
            if (count++) os << "\n";
            os << c.toProtocolString();
        }
    }
    dbLog("INFO", "searchByInstructor(" + name + ") - " +
          std::to_string(count) + " result(s)");
    return os.str();
}

std::string viewAll(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::ostringstream os;
    for (size_t i = 0; i < g_cache.size(); ++i) {
        if (i) os << "\n";
        os << g_cache[i].toProtocolString();
    }
    dbLog("INFO", "viewAll - returned " + std::to_string(g_cache.size()) +
          " course(s)");
    return os.str();
}

bool addCourse(const std::string& code, const std::string& title,
               const std::string& section, const std::string& instructor,
               const std::string& day, const std::string& time,
               const std::string& credits, const std::string& offeringUnit,
               const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    Course c{ trim(code), trim(title), trim(section), trim(instructor),
              trim(day),  trim(time),  trim(credits), trim(offeringUnit) };

    std::string err;
    if (!isValidCourse(c, err)) {
        dbLog("ERROR", "addCourse validation failed: " + err);
        return false;
    }

    const std::string key = courseRowDedupKey(c);
    for (const auto& ex : g_cache) {
        if (courseRowDedupKey(ex) == key) {
            dbLog("WARN", "addCourse: identical row already exists: " + key);
            return false;
        }
    }

    g_cache.push_back(c);
    g_cacheDirty = true;
    bool ok = flushCache_locked(filename);
    dbLog(ok ? "INFO" : "ERROR",
          "ADD " + c.code + " - " + (ok ? "SUCCESS" : "FAILED"));
    return ok;
}

bool updateCourse(const std::string& code, const std::string& field,
                  const std::string& newValue, const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::string key = toUpper(trim(code));
    std::string f   = toUpper(trim(field));
    std::string v   = trim(newValue);

    for (auto& c : g_cache) {
        if (toUpper(c.code) != key) continue;

        Course updated = c;
        if      (f == FIELD_TITLE)      updated.title      = v;
        else if (f == FIELD_SECTION)    updated.section    = v;
        else if (f == FIELD_INSTRUCTOR) updated.instructor = v;
        else if (f == FIELD_DAY)        updated.day        = v;
        else if (f == FIELD_TIME)       updated.time       = v;
        else if (f == FIELD_CREDITS)        updated.credits       = v;
        else if (f == FIELD_OFFERING_UNIT)  updated.offeringUnit  = v;
        else {
            dbLog("ERROR", "updateCourse: invalid field: " + field);
            return false;
        }

        std::string err;
        if (!isValidCourse(updated, err)) {
            dbLog("ERROR", "updateCourse validation failed: " + err);
            return false;
        }

        c = updated;
        g_cacheDirty = true;
        bool ok = flushCache_locked(filename);
        dbLog(ok ? "INFO" : "ERROR",
              "UPDATE " + code + " " + field + "=" + v +
              " - " + (ok ? "SUCCESS" : "FAILED"));
        return ok;
    }

    dbLog("WARN", "updateCourse: course not found: " + code);
    return false;
}

bool deleteCourse(const std::string& code, const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::string key = toUpper(trim(code));
    auto before = g_cache.size();
    g_cache.erase(
        std::remove_if(g_cache.begin(), g_cache.end(),
            [&](const Course& c){ return toUpper(c.code) == key; }),
        g_cache.end());

    if (g_cache.size() == before) {
        dbLog("WARN", "deleteCourse: course not found: " + code);
        return false;
    }

    g_cacheDirty = true;
    bool ok = flushCache_locked(filename);
    dbLog(ok ? "INFO" : "ERROR",
          "DELETE " + code + " - " + (ok ? "SUCCESS" : "FAILED"));
    return ok;
}

std::string searchByTimeSlot(const std::string& day, const std::string& time,
                             const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::string d = trim(day);
    std::string t = trim(time);
    std::ostringstream os;
    int count = 0;
    for (const auto& c : g_cache) {
        if (c.day == d && c.time == t) {
            if (count++) os << "\n";
            os << c.toProtocolString();
        }
    }
    dbLog("INFO", "searchByTimeSlot(" + d + "," + t + ") - " +
          std::to_string(count) + " result(s)");
    return os.str();
}

std::string searchByOfferingUnit(const std::string& offeringUnit,
                                  const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::string r = toLower(trim(offeringUnit));
    std::ostringstream os;
    int count = 0;
    for (const auto& c : g_cache) {
        if (toLower(c.offeringUnit) == r) {
            if (count++) os << "\n";
            os << c.toProtocolString();
        }
    }
    dbLog("INFO", "searchByOfferingUnit(" + offeringUnit + ") - " +
          std::to_string(count) + " result(s)");
    return os.str();
}

int getCourseCount(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);
    return static_cast<int>(g_cache.size());
}

std::vector<std::string> getUniqueInstructors(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::set<std::string> s;
    for (const auto& c : g_cache) s.insert(c.instructor);
    return { s.begin(), s.end() };
}

bool exportToJSON(const std::string& outPath, const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    ensureCacheLoaded_locked(filename);

    std::ofstream f(outPath, std::ios::trunc);
    if (!f.is_open()) {
        dbLog("ERROR", "exportToJSON: cannot open " + outPath);
        return false;
    }

    auto jesc = [](const std::string& s){
        std::string out; out.reserve(s.size() + 2);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
            }
        }
        return out;
    };

    f << "{\n  \"courses\": [\n";
    for (size_t i = 0; i < g_cache.size(); ++i) {
        const auto& c = g_cache[i];
        f << "    {"
          << "\"code\":\""       << jesc(c.code)       << "\","
          << "\"title\":\""      << jesc(c.title)      << "\","
          << "\"section\":\""    << jesc(c.section)    << "\","
          << "\"instructor\":\"" << jesc(c.instructor) << "\","
          << "\"day\":\""        << jesc(c.day)        << "\","
          << "\"time\":\""       << jesc(c.time)       << "\","
          << "\"credits\":\""        << jesc(c.credits)       << "\","
          << "\"offeringUnit\":\""   << jesc(c.offeringUnit)  << "\""
          << "}" << (i + 1 == g_cache.size() ? "" : ",") << "\n";
    }
    f << "  ],\n  \"count\": " << g_cache.size()
      << ",\n  \"exportedAt\": \"" << nowTimestamp() << "\"\n}\n";

    bool ok = f.good();
    dbLog(ok ? "INFO" : "ERROR",
          "exportToJSON -> " + outPath + " - " + (ok ? "SUCCESS" : "FAILED"));
    return ok;
}

bool backupDatabase(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    const std::string path = resolveDatabaseFile(filename);
    if (g_cacheValid && g_cachedFile == path) {
        flushCache_locked(path);
    }

    if (!fileExists(path)) {
        dbLog("ERROR", "backupDatabase: source missing: " + path);
        return false;
    }

    std::string backupPath = path + ".bak_" + timestampCompact();
    std::ifstream in(path, std::ios::binary);
    std::ofstream out(backupPath, std::ios::binary | std::ios::trunc);
    if (!in.is_open() || !out.is_open()) {
        dbLog("ERROR", "backupDatabase: cannot open files.");
        return false;
    }
    out << in.rdbuf();
    bool ok = in.good() || in.eof();
    ok = ok && out.good();

    dbLog(ok ? "INFO" : "ERROR",
          "backupDatabase -> " + backupPath + " - " +
          (ok ? "SUCCESS" : "FAILED"));
    return ok;
}

void invalidateCache() {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    g_cacheValid = false;
    g_cacheMTime = 0;
    dbLog("INFO", "Cache invalidated by request.");
}

void shutdownDatabase() {
    std::lock_guard<std::mutex> lock(g_dbMutex);
    if (g_cacheDirty && !g_cachedFile.empty()) {
        flushCache_locked(g_cachedFile);
    }
    g_cache.clear();
    g_cacheValid = false;
    dbLog("INFO", "Database module shutting down.");
}