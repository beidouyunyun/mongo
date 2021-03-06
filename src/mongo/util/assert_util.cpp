/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/util/assert_util.h"

#ifndef _WIN32
#include <cxxabi.h>
#include <sys/file.h>
#endif

#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>
#include <exception>

#include "mongo/config.h"
#include "mongo/logv2/log.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/debugger.h"
#include "mongo/util/exit.h"
#include "mongo/util/exit_code.h"
#include "mongo/util/log_global_settings.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/stacktrace.h"
#include "mongo/util/str.h"

namespace mongo {

AssertionCount assertionCount;

AssertionCount::AssertionCount() : regular(0), warning(0), msg(0), user(0), rollovers(0) {}

void AssertionCount::rollover() {
    rollovers.fetchAndAdd(1);
    regular.store(0);
    warning.store(0);
    msg.store(0);
    user.store(0);
}

void AssertionCount::condrollover(int newvalue) {
    static const int rolloverPoint = (1 << 30);
    if (newvalue >= rolloverPoint)
        rollover();
}

AtomicWord<bool> DBException::traceExceptions(false);

void DBException::traceIfNeeded(const DBException& e) {
    if (traceExceptions.load()) {
        LOGV2_WARNING(23075, "DBException thrown{causedBy_e}", "causedBy_e"_attr = causedBy(e));
        printStackTrace();
    }
}

MONGO_COMPILER_NOINLINE void verifyFailed(const char* expr, const char* file, unsigned line) {
    assertionCount.condrollover(assertionCount.regular.addAndFetch(1));
    LOGV2_ERROR(23076,
                "Assertion failure {expr} {file} {line}",
                "expr"_attr = expr,
                "file"_attr = file,
                "line"_attr = line);
    printStackTrace();
    std::stringstream temp;
    temp << "assertion " << file << ":" << line;

    breakpoint();
#if defined(MONGO_CONFIG_DEBUG_BUILD)
    // this is so we notice in buildbot
    LOGV2_FATAL(23078, "\n\n***aborting after verify() failure as this is a debug/test build\n\n");
    std::abort();
#endif
    error_details::throwExceptionForStatus(Status(ErrorCodes::UnknownError, temp.str()));
}

MONGO_COMPILER_NOINLINE void invariantFailed(const char* expr,
                                             const char* file,
                                             unsigned line) noexcept {
    LOGV2_FATAL(23079,
                "Invariant failure {expr} {file} {line}",
                "expr"_attr = expr,
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23080, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NOINLINE void invariantFailedWithMsg(const char* expr,
                                                    const std::string& msg,
                                                    const char* file,
                                                    unsigned line) noexcept {
    LOGV2_FATAL(23081,
                "Invariant failure {expr} {msg} {file} {line}",
                "expr"_attr = expr,
                "msg"_attr = msg,
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23082, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NOINLINE void invariantOKFailed(const char* expr,
                                               const Status& status,
                                               const char* file,
                                               unsigned line) noexcept {
    LOGV2_FATAL(23083,
                "Invariant failure: {expr} resulted in status {status} at {file} {line}",
                "expr"_attr = expr,
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23084, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NOINLINE void invariantOKFailedWithMsg(const char* expr,
                                                      const Status& status,
                                                      const std::string& msg,
                                                      const char* file,
                                                      unsigned line) noexcept {
    LOGV2_FATAL(23085,
                "Invariant failure: {expr} {msg} resulted in status {status} at {file} {line}",
                "expr"_attr = expr,
                "msg"_attr = msg,
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23086, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NOINLINE void invariantStatusOKFailed(const Status& status,
                                                     const char* file,
                                                     unsigned line) noexcept {
    LOGV2_FATAL(23087,
                "Invariant failure {status} at {file} {line}",
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23088, "\n\n***aborting after invariant() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NOINLINE void fassertFailedWithLocation(int msgid,
                                                       const char* file,
                                                       unsigned line) noexcept {
    LOGV2_FATAL(23089,
                "Fatal Assertion {msgid} at {file} {line}",
                "msgid"_attr = msgid,
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23090, "\n\n***aborting after fassert() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NOINLINE void fassertFailedNoTraceWithLocation(int msgid,
                                                              const char* file,
                                                              unsigned line) noexcept {
    LOGV2_FATAL(23091,
                "Fatal Assertion {msgid} at {file} {line}",
                "msgid"_attr = msgid,
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23092, "\n\n***aborting after fassert() failure\n\n");
    quickExit(EXIT_ABRUPT);
}

MONGO_COMPILER_NORETURN void fassertFailedWithStatusWithLocation(int msgid,
                                                                 const Status& status,
                                                                 const char* file,
                                                                 unsigned line) noexcept {
    LOGV2_FATAL(23093,
                "Fatal assertion {msgid} {status} at {file} {line}",
                "msgid"_attr = msgid,
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23094, "\n\n***aborting after fassert() failure\n\n");
    std::abort();
}

MONGO_COMPILER_NORETURN void fassertFailedWithStatusNoTraceWithLocation(int msgid,
                                                                        const Status& status,
                                                                        const char* file,
                                                                        unsigned line) noexcept {
    LOGV2_FATAL(23095,
                "Fatal assertion {msgid} {status} at {file} {line}",
                "msgid"_attr = msgid,
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    breakpoint();
    LOGV2_FATAL(23096, "\n\n***aborting after fassert() failure\n\n");
    quickExit(EXIT_ABRUPT);
}

MONGO_COMPILER_NOINLINE void uassertedWithLocation(const Status& status,
                                                   const char* file,
                                                   unsigned line) {
    assertionCount.condrollover(assertionCount.user.addAndFetch(1));
    LOGV2_DEBUG(23074,
                1,
                "User Assertion: {status} {file} {line}",
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    error_details::throwExceptionForStatus(status);
}

MONGO_COMPILER_NOINLINE void msgassertedWithLocation(const Status& status,
                                                     const char* file,
                                                     unsigned line) {
    assertionCount.condrollover(assertionCount.msg.addAndFetch(1));
    LOGV2_ERROR(23077,
                "Assertion: {status} {file} {line}",
                "status"_attr = redact(status),
                "file"_attr = file,
                "line"_attr = line);
    error_details::throwExceptionForStatus(status);
}

std::string causedBy(StringData e) {
    constexpr auto prefix = " :: caused by :: "_sd;
    std::string out;
    out.reserve(prefix.size() + e.size());
    out.append(prefix.rawData(), prefix.size());
    out.append(e.rawData(), e.size());
    return out;
}

std::string causedBy(const char* e) {
    return causedBy(StringData(e));
}

std::string causedBy(const DBException& e) {
    return causedBy(e.toString());
}

std::string causedBy(const std::exception& e) {
    return causedBy(e.what());
}

std::string causedBy(const std::string& e) {
    return causedBy(StringData(e));
}

std::string causedBy(const Status& e) {
    return causedBy(e.toString());
}

std::string demangleName(const std::type_info& typeinfo) {
#ifdef _WIN32
    return typeinfo.name();
#else
    int status;

    char* niceName = abi::__cxa_demangle(typeinfo.name(), nullptr, nullptr, &status);
    if (!niceName)
        return typeinfo.name();

    std::string s = niceName;
    free(niceName);
    return s;
#endif
}

Status exceptionToStatus() noexcept {
    try {
        throw;
    } catch (const DBException& ex) {
        return ex.toStatus();
    } catch (const std::exception& ex) {
        return Status(ErrorCodes::UnknownError,
                      str::stream() << "Caught std::exception of type " << demangleName(typeid(ex))
                                    << ": " << ex.what());
    } catch (const boost::exception& ex) {
        return Status(ErrorCodes::UnknownError,
                      str::stream()
                          << "Caught boost::exception of type " << demangleName(typeid(ex)) << ": "
                          << boost::diagnostic_information(ex));

    } catch (...) {
        LOGV2_FATAL(23097, "Caught unknown exception in exceptionToStatus()");
        std::terminate();
    }
}
}  // namespace mongo
