/******************************************************************************
 * Copyright (c) 2021, Vayavya Labs Pvt. Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Vayavya Labs Pvt. Ltd. nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VAYAVYA LABS PVT. LTD. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/


#include "../inc/csml_logger.h"
#include <iostream>
#include <systemc>

std::unique_ptr<std::ofstream> CsmlLogger::globalLogFileStream_;
std::string CsmlLogger::globalLogFilePath_;
bool CsmlLogger::globalLogEnabled_ = false;

void CsmlLogger::setGlobalLogFile(const std::string& path) {
    globalLogFilePath_ = path;
    globalLogFileStream_ = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::app);
    globalLogEnabled_ = globalLogFileStream_->is_open();
}

void CsmlLogger::disableGlobalLogFile() {
    globalLogEnabled_ = false;
    globalLogFileStream_.reset();
    globalLogFilePath_.clear();
}

bool CsmlLogger::globalLogFileEnabled() {
    return globalLogEnabled_ && globalLogFileStream_ && globalLogFileStream_->is_open();
}

std::ostream* CsmlLogger::globalLogFileStream() {
    if (!globalLogEnabled_ || !globalLogFileStream_ || !globalLogFileStream_->is_open())
        return nullptr;
    return globalLogFileStream_.get();
}

std::string formatLogMessage(const std::map<std::string, std::string>& tokens, std::string format) {
    std::string result = format;
    for (const auto& [token, value] : tokens) {
        size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return result;
}

std::string getModuleName() {
    auto obj = sc_core::sc_get_current_object();
    if (!obj) return "";
    const char* n = obj->name();
    return n ? n : "";
}

LogStream::LogStream(const std::map<std::string, std::string>& tokens, std::string format)
    : tokens_(tokens), format(format) {}

LogStream::~LogStream() {
    auto t = tokens_;
    t["%MESSAGE%"] = this->str();
    const auto msg = formatLogMessage(t, format);
    std::cout << msg << std::endl;
    if (auto* out = CsmlLogger::globalLogFileStream()) {
        (*out) << msg << std::endl;
        out->flush();
    }
}

LogStream csmlLog(const std::string& severity, int verbosity, const char* func, CsmlLogger& logger) {
    std::map<std::string, std::string> tokens = {
        {"%MODULE%", getModuleName()},
        {"%LEVEL%", severity},
        {"%VERBOSITY%", std::to_string(verbosity)},
        {"%TIME%", sc_core::sc_time_stamp().to_string()},
        {"%FUNCTION%", (func && *func) ? std::string(func) : std::string("unknown")},
        {"%MESSAGE%", ""}
    };
    return LogStream(tokens, logger.getLogFormat());
}

FunctionTracer::FunctionTracer(const char* func, CsmlLogger& logger)
    : funcName(func), logger(logger) {
    if (logger.getFunctionTrace() == true)
        csmlLog("trace", csml_severity::CSML_DEBUG, funcName, logger) << "Entered " << funcName;
}

FunctionTracer::~FunctionTracer() {
    if (logger.getFunctionTrace() == true)
        csmlLog("trace", csml_severity::CSML_DEBUG, funcName, logger) << "Exiting " << funcName;
}
