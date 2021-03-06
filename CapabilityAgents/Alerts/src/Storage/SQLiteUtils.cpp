/*
 * SqlLiteUtils.cpp
 *
 * Copyright 2017 Amazon.com, Inc. or its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Alerts/Storage/SQLiteUtils.h"

#include "Alerts/Storage/SQLiteStatement.h"

#include <AVSCommon/Utils/Logger/Logger.h>
#include <AVSCommon/Utils/String/StringUtils.h>

#include <fstream>

namespace alexaClientSDK {
namespace capabilityAgents {
namespace alerts {
namespace storage {

using namespace avsCommon::utils::logger;
using namespace avsCommon::utils::string;

/// String to identify log entries originating from this file.
static const std::string TAG("SQLiteUtils");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

/**
 * A small utility function to help determine if a file exists.
 *
 * @param filePath The path to the file being queried about.
 * @return Whether the file exists and is accessible.
 */
static bool fileExists(const std::string & filePath) {

    // TODO - make this more portable and dependable.
    // https://issues.labcollab.net/browse/ACSDK-380

    std::ifstream is(filePath);
    return is.good();
}

/**
 * A utility function to open or create a SQLite database, depending on the flags being passed in.
 * The possible flags defined by SQLite for this operation are as follows:
 *
 * SQLITE_OPEN_READONLY
 * SQLITE_OPEN_READWRITE
 * SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
 * 
 * The meanings of these flags are as one might expect, however for further details please refer to the online
 * documentation here:
 *
 * https://sqlite.org/c3ref/open.html
 *
 * @param filePath The path, including file name, to where the database is to be opened or created.
 * @param sqliteFlags Flags which will be passed to the SQLite call.  These flags determine the method of opening.
 * @param[out] dbHandle A double-pointer to a sqlite3 handle which will be updated to point to a valid handle.
 * @return Whether the operation was successful.
 */
static sqlite3* openSQLiteDatabaseHelper(const std::string & filePath, int sqliteFlags) {
    sqlite3* dbHandle = nullptr;

    int rcode = sqlite3_open_v2(
            filePath.c_str(), // file path
            &dbHandle,        // the db handle
            sqliteFlags,      // flags
            nullptr);         // optional vfs name (C-string)

    if (rcode != SQLITE_OK) {
        ACSDK_ERROR(LX("openSQLiteDatabaseHelperFailed")
                .m("Could not open database.")
                .d("rcode", rcode)
                .d("file path", filePath)
                .d("error message", sqlite3_errmsg(dbHandle)));
        sqlite3_close(dbHandle);
        dbHandle = nullptr;
    }

    return dbHandle;
}

sqlite3* createSQLiteDatabase(const std::string & filePath) {
    if (fileExists(filePath)) {
        ACSDK_ERROR(LX("createSQLiteDatabaseFailed").m("File already exists.").d("file", filePath));
        return nullptr;
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    sqlite3* dbHandle = openSQLiteDatabaseHelper(filePath, flags);

    if (!dbHandle) {
        ACSDK_ERROR(LX("createSQLiteDatabaseFailed").m("Could not create database."));
    }

    return dbHandle;
}

sqlite3* openSQLiteDatabase(const std::string & filePath) {
    if (!fileExists(filePath)) {
        ACSDK_ERROR(LX("openSQLiteDatabaseFailed").m("File could not be found.").d("file", filePath));
        return nullptr;
    }

    int flags = SQLITE_OPEN_READWRITE;
    sqlite3* dbHandle = openSQLiteDatabaseHelper(filePath, flags);

    if (!dbHandle) {
        ACSDK_ERROR(LX("openSQLiteDatabaseFailed").m("Could not open database."));
    }

    return dbHandle;
}

bool closeSQLiteDatabase(sqlite3* dbHandle) {
    if (!dbHandle) {
        ACSDK_ERROR(LX("closeSQLiteDatabaseFailed").m("dbHandle is nullptr."));
    }

    int rcode = sqlite3_close(dbHandle);

    if (rcode != SQLITE_OK) {
        ACSDK_ERROR(LX("closeSQLiteDatabaseFailed")
                .d("rcode", rcode)
                .d("error message", sqlite3_errmsg(dbHandle)));
        return false;
    }

    return true;
}

bool performQuery(sqlite3* dbHandle, const std::string & sqlString) {
    if (!dbHandle) {
        ACSDK_ERROR(LX("performQueryFailed").m("dbHandle was nullptr."));
        return false;
    }

    int rcode = sqlite3_exec(
            dbHandle,           // handle to the open database
            sqlString.c_str(),  // the SQL query
            nullptr,            // optional callback function
            nullptr,            // first argument to optional callback function
            nullptr);           // optional pointer to error message (char**)

    if (rcode != SQLITE_OK) {
        ACSDK_ERROR(LX("performQueryFailed")
                .m("Could not execute SQL:" + sqlString)
                .d("rcode", rcode)
                .d("error message", sqlite3_errmsg(dbHandle)));
        return false;
    }

    return true;
}

bool getNumberTableRows(sqlite3* dbHandle, const std::string & tableName, int* numberRows) {
    if (!dbHandle) {
        ACSDK_ERROR(LX("getNumberTableRowsFailed").m("dbHandle was nullptr."));
        return false;
    }

    if (!numberRows) {
        ACSDK_ERROR(LX("getNumberTableRowsFailed").m("dbHandle was nullptr."));
        return false;
    }

    std::string sqlString = "SELECT COUNT(*) FROM " + tableName + ";";
    SQLiteStatement statement(dbHandle, sqlString);

    if (!statement.isValid()) {
        ACSDK_ERROR(LX("getNumberTableRowsFailed").m("Could not create statement."));
        return false;
    }

    if (!statement.step()) {
        ACSDK_ERROR(LX("getNumberTableRowsFailed").m("Could not step to next row."));
        return false;
    }

    const int RESULT_COLUMN_POSITION = 0;
    std::string rowValue = statement.getColumnText(RESULT_COLUMN_POSITION);

    if (!stringToInt(rowValue.c_str(), numberRows)) {
        ACSDK_ERROR(LX("getNumberTableRowsFailed").d("Could not convert string to integer", rowValue));
        return false;
    }

    return true;
}

bool getTableMaxIntValue(sqlite3* dbHandle, const std::string & tableName, const std::string & columnName, int* maxId) {
    if (!maxId) {
        ACSDK_ERROR(LX("getMaxIdFailed").m("dbHandle was nullptr."));
        return false;
    }

    std::string sqlString = "SELECT " + columnName + " FROM " + tableName +
            " ORDER BY " + columnName + " DESC LIMIT 1;";

    SQLiteStatement statement(dbHandle, sqlString);

    if (!statement.isValid()) {
        ACSDK_ERROR(LX("getTableMaxIntValueFailed").m("Could not create statement."));
        return false;
    }

    if (!statement.step()) {
        ACSDK_ERROR(LX("getTableMaxIntValueFailed").m("Could not step to next row."));
        return false;
    }

    int stepResult = statement.getStepResult();

    if (stepResult != SQLITE_ROW && stepResult != SQLITE_DONE) {
        ACSDK_ERROR(LX("getTableMaxIntValueFailed").m("Step did not evaluate to either row or completion."));
        return false;
    }

    // No entries were found in database - set to zero as the current max id.
    if (SQLITE_DONE == stepResult) {
        *maxId = 0;
    }

    // Entries were found - let's get the value.
    if (SQLITE_ROW == stepResult) {
        const int RESULT_COLUMN_POSITION = 0;
        std::string rowValue = statement.getColumnText(RESULT_COLUMN_POSITION);

        if (!stringToInt(rowValue.c_str(), maxId)) {
            ACSDK_ERROR(LX("getTableMaxIntValueFailed").d("Could not convert string to integer", rowValue));
            return false;
        }
    }

    return true;
}

bool clearTable(sqlite3* dbHandle, const std::string & tableName) {
    if (!dbHandle) {
        ACSDK_ERROR(LX("clearTableFailed").m("dbHandle was nullptr."));
        return false;
    }

    std::string sqlString = "DELETE FROM " + tableName + ";";

    SQLiteStatement statement(dbHandle, sqlString);

    if (!statement.isValid()) {
        ACSDK_ERROR(LX("clearTableFailed").m("Could not create statement."));
        return false;
    }

    if (!statement.step()) {
        ACSDK_ERROR(LX("clearTableFailed").m("Could not perform step."));
        return false;
    }

    return true;
}

} // namespace storage
} // namespace alerts
} // namespace capabilityAgents
} // namespace alexaClientSDK