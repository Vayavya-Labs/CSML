/**
 * @file configfile_parser.h
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

#ifdef ACCELLERA_CCI_STD
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
                    std::cout << "[config_parser] WARNING: @include file not found: " << incfile << std::endl;
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
#endif

