#include <string.h>
#include <sstream>
#include <assert.h>

#include "util.h"


void init_string(mpop_string *str)
{
  memset(str, 0, sizeof(mpop_string));
}

void free_string(mpop_string *str)
{
  if (str->string) free(str->string);
  init_string(str);
}

void clear_string(mpop_string *str)
{
  if (str->string) *str->string  = 0;
  str->size = 0;
}

void allocate_string(mpop_string *str, int need_size)
{
  if (str->alloc_size <= need_size)
  {
    str->alloc_size = (need_size + 255) & ~255;
    if (str->string)
    {
      str->string = (char*)realloc(str->string, str->alloc_size);
    }
    else
    {
      str->string = (char*)malloc(str->alloc_size);
      *str->string = 0;
    }
    assert(str->string!=NULL);
  }
}

void add_stringn(mpop_string *str, const void *p, int n)
{
  char *np;

  assert( p && str );

  allocate_string(str, str->size + n + 1);
  np = str->string + str->size;
  memcpy(np, p, n);
  np[n] = 0;
  str->size += n;
}

void add_string(mpop_string *str, const char *p)
{
  add_stringn(str, p, strlen(p));
}

void add_char(mpop_string *str, char c)
{
  add_stringn(str, &c, 1);
}


const char* strcasestr(const char* str, const char* pattern)
{
    for( ;*str; ++str)
    {
        if(tolower(*str) == tolower(*pattern))
        {
            const char *s, *p;
            for(s = str, p = pattern ; *s && *p; ++s, ++p)
            {
                if(tolower(*s) != tolower(*p))
                    break;
            }
            if(*p == '\0')
                return str;
        }
    }
    return NULL;
}

int mb_compare_string_p(const void *p1, const void *p2)
{
    return strcmp(* (char * const *) p1, * (char * const *) p2);
}

//static
bool UTIL::isUrl(const char* str, int len)
/*
    return true, if attribute value starts with some internet protocol.
*/
{
    if( (len > 1) && str[0] == '/' && str[1] == '/') return true;

    if(strncasecmp(str,"www", 3) == 0) return true;

    if(strncasecmp(str,"http://", 7)  == 0) return true;
    if(strncasecmp(str,"https://", 8) == 0) return true;
    if(strncasecmp(str,"ftp://", 6)   == 0) return true;
    if(strncasecmp(str,"afs://", 6)   == 0) return true;
    if(strncasecmp(str,"wais://", 7)  == 0) return true;
    if(strncasecmp(str,"telnet://", 9)== 0) return true;
    if(strncasecmp(str,"gopher://", 9)== 0) return true;
    if(strncasecmp(str,"cid:", 4)     == 0) return true;
    if(strncasecmp(str,"news:", 5)    == 0) return true;
    if(strncasecmp(str,"nntp:", 5)    == 0) return true;
    if(strncasecmp(str,"mid:", 4)     == 0) return true;
    if(strncasecmp(str,"skype:", 6)   == 0) return true;
    if(strncasecmp(str,"prospero:", 9)== 0) return true;
    if(strncasecmp(str,"mailto:", 7)  == 0) return true;

    return false;
}


//static
bool UTIL::isBeginFromUrl(const char* str, int len)
/*
    found strings like '{{http://www.ya.ru}}'
*/
{

    if( (len > 1) && str[0] == '/' && str[1] == '/') return true;

    const char* tmp = str;
    while (*tmp && !isalpha(*tmp))
    {
        ++tmp;
        --len;
    }
    return UTIL::isUrl(tmp, len);
}


//static
size_t UTIL::split(std::string _str, const std::string& _delim, std::vector<std::string>& ret)
{
    std::string::size_type cut_at;
    size_t count = 0;

    while ((cut_at = _str.find_first_of(_delim)) != _str.npos)
    {
        if (cut_at > 0)
        {
            ret.push_back(_str.substr(0, cut_at));
            ++count;
        }
        _str = _str.substr(cut_at + 1);
    }

    if (_str.length() > 0)
    {
        ret.push_back( _str );
        ++count;
    }

    return count;
}

//static
uint32_t UTIL::s2i(const std::string& s) {
    uint32_t ret=strtoul(s.c_str(), NULL, 10);
    return ret;
}

//static
std::string UTIL::i2s(int i)
{
    std::string ret;
    std::stringstream ss;
    ss << i;
    ss >> ret;
    return ret;
}

//static
bool UTIL::startsWith(const std::string &s, const char *prefix)
{
     return !strncmp(s.c_str(), prefix, strlen(prefix));
}
