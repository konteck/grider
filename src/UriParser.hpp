#include <iostream>
#include <string>
#include <stdlib.h>


namespace http {
    struct url {
        std::string protocol, user, password, host, path, search;
        int port;
    };


    //--- Helper Functions -------------------------------------------------------------~
    std::string TailSlice(std::string &subject, std::string delimiter, bool keep_delim=false) {
        // Chops off the delimiter and everything that follows (destructively)
        // returns everything after the delimiter
        int delimiter_location = subject.find(delimiter);
        int delimiter_length = delimiter.length();
        std::string output = "";

        if (delimiter_location < std::string::npos) {
            int start = keep_delim ? delimiter_location : delimiter_location + delimiter_length;
            int end = subject.length() - start;
            output = subject.substr(start, end);
            subject = subject.substr(0, delimiter_location);
        }
        return output;
    }

    std::string HeadSlice(std::string &subject, std::string delimiter) {
        // Chops off the delimiter and everything that precedes (destructively)
        // returns everthing before the delimeter
        int delimiter_location = subject.find(delimiter);
        int delimiter_length = delimiter.length();
        std::string output = "";
        if (delimiter_location < std::string::npos) {
            output = subject.substr(0, delimiter_location);
            subject = subject.substr(delimiter_location + delimiter_length, subject.length() - (delimiter_location + delimiter_length));
        }
        return output;
    }


    //--- Extractors -------------------------------------------------------------------~
    int ExtractPort(std::string &hostport) {
        int port;
        std::string portstring = TailSlice(hostport, ":");
        try { port = atoi(portstring.c_str()); }
        catch (std::exception e) { port = -1; }
        return port;
    }

    std::string ExtractPath(std::string &in) { return TailSlice(in, "/", true); }
    std::string ExtractProtocol(std::string &in) { return HeadSlice(in, "://"); }
    std::string ExtractSearch(std::string &in) { return TailSlice(in, "?"); }
    std::string ExtractPassword(std::string &userpass) { return TailSlice(userpass, ":"); }
    std::string ExtractUserpass(std::string &in) { return HeadSlice(in, "@"); }


    //--- Public Interface -------------------------------------------------------------~
    url ParseHttpUrl(std::string &in) {
        url ret;
        ret.protocol = ExtractProtocol(in);
        ret.search = ExtractSearch(in);
        ret.path = ExtractPath(in);
        std::string userpass = ExtractUserpass(in);
        ret.password = ExtractPassword(userpass);
        ret.user = userpass;
        ret.port = ExtractPort(in);
        ret.host = in;

        return ret;
    }
}
