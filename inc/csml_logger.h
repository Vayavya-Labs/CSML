#pragma once

#include <string>
#include <map>
#include <sstream>
#include <iostream>
#include <fstream>
#include <memory>
#include <systemc>

using namespace sc_core;

enum csml_severity {
    CSML_DEBUG = 3,
    CSML_INFO  = 2,
    CSML_WARN  = 1,
    CSML_ERROR = 0
};


// Macro for logging messages based on verbosity level
// sev: Severity level (e.g., "trace", "debug", "info", "warn", "error")
// level: Verbosity level (integer)
// __FUNCTION__: Predefined macro that holds the name of the current function
//logger: instance of CsmlLogger
#define CSML_LOG(sev, level, logger) \
    if ((level) > (logger).getMaxVerbosity()) {} \
    else csmlLog(sev, level, __FUNCTION__, logger)

// Convenience macros for different log levels
#define CSML_DEBUG(level, logger) CSML_LOG("DEBUG", level, logger)
#define CSML_INFO(level, logger)  CSML_LOG("INFO", level, logger)
#define CSML_WARN(level, logger)  csmlLog("WARN", level, __FUNCTION__, logger)
#define CSML_ERROR(level, logger) csmlLog("ERROR", level, __FUNCTION__, logger)

// Macro to trace function entry and exit
#define CSML_FUNC_TRACE(logger) FunctionTracer __csmlFuncTracerObj(__FUNCTION__, logger)

//Macro to trace Variables - last argument can be value of the variable
#define CSML_VAR_TRACE(type, var, logger, ...) VariableTracer<type> var{logger, #var, __VA_ARGS__}

// Logger class
class CsmlLogger {
private:
    int maxVerbosity;
    std::string logFormat;
    bool enableFunctionTrace;
    bool enableVariableTrace;

    // Global CSML log file sink. Configure once at startup (e.g. from the platform top)
    // and do not toggle at runtime.
    static std::unique_ptr<std::ofstream> globalLogFileStream_;
    static std::string globalLogFilePath_;
    static bool globalLogEnabled_;

public:
    CsmlLogger()
        : maxVerbosity(csml_severity::CSML_DEBUG),
          logFormat("[%TIME%] [%LEVEL% %VERBOSITY%] [%MODULE%::%FUNCTION%] - %MESSAGE%"),
          enableFunctionTrace(true),
          enableVariableTrace(true) {}

    void setMaxVerbosity(int level) { maxVerbosity = level; }
    int getMaxVerbosity() const { return maxVerbosity; }
    bool getFunctionTrace() { return enableFunctionTrace; }
    bool getVariableTrace() { return enableVariableTrace; }

    void setLogFormat(const std::string& format) { logFormat = format; }
    std::string getLogFormat() const { return logFormat; }
    void setFunctionTrace(bool value) { enableFunctionTrace = value; }
    void setVariableTrace(bool value) { enableVariableTrace = value; }

    static void setGlobalLogFile(const std::string& path);
    static void disableGlobalLogFile();
    static bool globalLogFileEnabled();
    static std::ostream* globalLogFileStream();
};

// Forward declarations
std::string formatLogMessage(const std::map<std::string, std::string>& tokens, std::string format);
std::string getModuleName();

class LogStream : public std::ostringstream {
public:
    LogStream(const std::map<std::string, std::string>& tokens, std::string format);
    ~LogStream() override;

private:
    std::map<std::string, std::string> tokens_;
    std::string format;
};

LogStream csmlLog(const std::string& severity, int verbosity, const char* func, CsmlLogger& logger);

class FunctionTracer {
public:
    FunctionTracer(const char* func, CsmlLogger& logger);
    ~FunctionTracer();

private:
    const char* funcName;
    CsmlLogger& logger;

};

// Templated helper for traced variables. It wraps a value of type T, logs assignments
// (printing old and new values) using the provided CsmlLogger.
template<typename T>
class VariableTracer {
public:
    VariableTracer(CsmlLogger& logger, const char* name, const T &value)
        : m_logger(logger), m_var_name(name), m_value(value) {}
    VariableTracer(CsmlLogger& logger, const char* name)
        : m_logger(logger), m_var_name(name) {}

    // Assignment from raw value: log old and new values, then assign
    VariableTracer& operator=(const T& v) {
        if (m_logger.getVariableTrace() == true) {
            csmlLog("trace", csml_severity::CSML_DEBUG, "", m_logger)
                << m_var_name << ": " << m_value << " -> " << v;
        }
        m_value = v;
        return *this;
    }

    VariableTracer& operator=(const VariableTracer& other) {
        return operator=(static_cast<T>(other));
    }

    // Implicit cast so the helper can be used wherever T is expected
    operator T() const { return m_value; }

    // Compound operators (log via assignment path)
    VariableTracer& operator+=(const T& rhs) { return (*this = static_cast<T>(m_value + rhs)); }
    VariableTracer& operator-=(const T& rhs) { return (*this = static_cast<T>(m_value - rhs)); }
    VariableTracer& operator*=(const T& rhs) { return (*this = static_cast<T>(m_value * rhs)); }
    VariableTracer& operator/=(const T& rhs) { return (*this = static_cast<T>(m_value / rhs)); }
    VariableTracer& operator%=(const T& rhs) { return (*this = static_cast<T>(m_value % rhs)); }
    VariableTracer& operator&=(const T& rhs) { return (*this = static_cast<T>(m_value & rhs)); }
    VariableTracer& operator|=(const T& rhs) { return (*this = static_cast<T>(m_value | rhs)); }
    VariableTracer& operator^=(const T& rhs) { return (*this = static_cast<T>(m_value ^ rhs)); }
    VariableTracer& operator<<=(const T& rhs) { return (*this = static_cast<T>(m_value << rhs)); }
    VariableTracer& operator>>=(const T& rhs) { return (*this = static_cast<T>(m_value >> rhs)); }

    // Increment/Decrement
    VariableTracer& operator++() { return (*this = static_cast<T>(m_value + 1)); }
    T operator++(int) { T tmp = m_value; *this = static_cast<T>(m_value + 1); return tmp; }
    VariableTracer& operator--() { return (*this = static_cast<T>(m_value - 1)); }
    T operator--(int) { T tmp = m_value; *this = static_cast<T>(m_value - 1); return tmp; }


private:
    CsmlLogger& m_logger;
    const char* m_var_name;
    T m_value;
};



