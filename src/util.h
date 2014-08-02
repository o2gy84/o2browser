#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>

typedef struct mpop_string
{
    char *string;
    int  size;
    int  alloc_size;
}
mpop_string;

void init_string(mpop_string *str);
void free_string(mpop_string *str);
void clear_string(mpop_string *str);
void add_char(mpop_string *str, char c);
void add_string(mpop_string *str, const char *s);
void add_stringn(mpop_string *str, const void *s, int size);
void allocate_string(mpop_string *str, int size);
int mb_compare_string_p(const void *p1, const void *p2);

const char *strcasestr(const char* str, const char* pattern);

class UTIL
{
public:

    static bool isUrl(const char* str, int len);
    static bool isBeginFromUrl(const char* str, int len);
    static size_t split(std::string _str, const std::string& _delim, std::vector<std::string>& ret);
    static uint32_t s2i(const std::string& s);                // string --> int
    static std::string i2s(int i);                            // int --> string
    static bool startsWith(const std::string &s, const char *prefix);
};


#endif // UTIL_H
