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


/**
 * @file csml_config_parser.h
 * @author  
 * @version 
 *
 * @section LICENSE
 *
 * @section DESCRIPTION
 * File contains a function to parse the configuration file
 *
 */
#pragma once

#include <iostream>
#include <map>
#include <fstream>
#include <sstream>

// This is used when CCI parameters are enabled.
// The format of the configuration file is same as that required by SCML
// When SCML parameters is enabled, the parser is part of the SCML library
inline std::map<std::string, std::string> cci_configfile_parser(std::string cci_config_file)
{
    std::string s, key, value;
    std::map <std::string, std::string> cci_params;
    std::ifstream f(cci_config_file);
    std::string val_type;
    while(std::getline( f, s ))
    {
        // Find the first valid start skipping tabs and form feed
        std::string::size_type begin = s.find_first_not_of( " \f\t\v" );

        // Skip blank lines
        if(begin == std::string::npos) continue;
        // Skip comments
        if(std::string( "#;//" ).find( s[ begin ] ) != std::string::npos) continue;

        // @include directive — recursively merge another ini file
        if (s[begin] == '@') {
            std::istringstream ss(s.substr(begin + 1));
            std::string directive, incfile;
            ss >> directive >> incfile;
            if (directive == "include" && !incfile.empty()) {
                auto slash = cci_config_file.find_last_of("/\\");
                if (slash != std::string::npos)
                    incfile = cci_config_file.substr(0, slash + 1) + incfile;
                std::ifstream probe(incfile);
                if (!probe.is_open())
                    std::cout << "[csml_config_parser] WARNING: @include file not found: " << incfile << std::endl;
                else {
                    probe.close();
                    auto sub = cci_configfile_parser(incfile);
                    cci_params.insert(sub.begin(), sub.end());
                }
            }
            continue;
        }

        // Section header of the form: [type]
        // Only treat it as a header if the first non-whitespace character is '['.
        // This allows JSON-like array values such as: key : [4000]
        if(s[begin] == '[')
        {
            // Find the position of the closing bracket ']'
            std::string::size_type last = s.find(']', begin + 1);
            if(last != std::string::npos && last > begin + 1)
            {
                // Extract the content between the brackets
                val_type = s.substr(begin + 1, last - begin - 1);
                continue;
            }
        }

        // Extract the key value
        std::string::size_type end = s.find( ':', begin );
        key = s.substr( begin, end - begin );

        // Remove any trailing white spaces of the key
        key.erase(key.find_last_not_of(" \f\t\r\v") + 1 );

        // Valid key values containing only letters, numbers, . and _
        if(key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890_.") != std::string::npos)
        {
            std::cout << "Error parsing input cci parameter configuration file" << std::endl;
            // Empty the map
            cci_params.clear();
            return cci_params;
        }

        // No blank keys allowed
        if(key.empty()) continue;

        // Extract the value (no leading or trailing whitespace allowed)
        begin = s.find_first_not_of( " \f\n\r\t\v", end + 1 );
        end   = s.find_last_not_of(  " \f\n\r\t\v" ) + 1;

        value = s.substr( begin, end - begin );

        // If the configuration is CSML and CCI, string values from the INI file needs to be present inside double quotes ("").
        // For SCML, if the values are strings, they are considered as string data type even without double quotes.
        // Insert " (double quotes) at the beginning and ending of the string
        if(val_type == "string")
        {
            value = "\"" + value + "\"";
        }
        // Insert  (key, value) pair into the map
        cci_params[key] = value;
    }

    f.close();
    return cci_params;
}

