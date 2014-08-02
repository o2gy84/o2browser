#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>

class UTIL {
public:

    static bool isUrl(const char* str, int len);
    static bool isBeginFromUrl(const char* str, int len);
    static size_t split(std::string _str, const std::string& _delim, std::vector<std::string>& ret);
    static uint32_t s2i(const std::string& s);                // string --> int
    static std::string i2s(int i);                            // int --> string
    static bool startsWith(const std::string &s, const char *prefix);
};


#endif // UTIL_H
