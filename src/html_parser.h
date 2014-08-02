#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#define _GNU_SOURCE

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <strings.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <string>


class HtmlParser
{
public:
    HtmlParser()
    {
        init_params();
    }
    ~HtmlParser()
    {
        deinit_params();
    }

    int parse(const std::string &html);

    std::string getHtml() {return m_HtmlText;}
    std::string getPlain() {return m_PlainText;}

private:
    int parse(char* _str, int _is_corp, int _img_jigurda, int _is_quote_collapse_user, int _user_id, long _msglist_flags);
    int init_params();
    int deinit_params();

private:
    std::string m_HtmlText;
    std::string m_PlainText;
};


#endif // HTML_PARSER_H
