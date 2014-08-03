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
#include <vector>


#define kMaxTagLen           256			// max len of tag name
#define kMaxAttrLen          256			// max len of attribute name
#define kMaxAttrValueLen     65535			// max len of attribute value
#define kMaxFullClosedTagLen 65535			// max len of full html construction like   <some_tag ...bla...bla...bla.. / >
#define kMemcKeyLen          250

typedef struct {
    char   name[kMaxTagLen];	// name of tag
    bool   open;		    	// true if <tag>, false if </tag>
    size_t position;		    // tag position in letter
} tag_t;

typedef struct xtext_t {		// using for ban images
    struct xtext_t* next;
    char		memc_key[kMemcKeyLen+1];
    size_t    	position;
    size_t   	len;
} xtext_t;

typedef struct {			// list of images, who would be scanned in memcached for ban
    xtext_t* first;
    xtext_t* last;
    size_t   count;			// количество найденных картинок
    size_t   total_len;		// общая длина всех урлов картинок
} xtext_list_t;


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

    std::string getHtml() const {return m_HtmlText;}
    std::string getPlain() const {return m_PlainText;}
    std::vector<std::string> getLinks() const {return m_Links;}

private:
    int parse(char* _str, int _is_corp, int _img_jigurda, int _is_quote_collapse_user, int _user_id, long _msglist_flags);
    int init_params();
    int deinit_params();

    int check_style(const char* _str, int _len, char _external_quote, char* inner_quot, char*  _res,
                        const char** _black_ref_list, size_t _b_ref_list_size);

    int xss_attribute_parsing(const char* _begin, const char* _end, tag_t* tag,
                                            const char** _black_list, size_t _b_list_size,
                                            const char** _black_ref_list, size_t _b_ref_list_size,
                                            xtext_list_t* _xtext_list, char* _attr_str);

    int url_in_style_process(char* str, const char** _black_ref_list, size_t _b_ref_list_size);

private:
    std::string m_HtmlText;
    std::string m_PlainText;
    std::vector<std::string> m_Links;
};


#endif // HTML_PARSER_H
