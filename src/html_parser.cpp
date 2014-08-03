#include <iostream>

#include "html_parser.h"
#include "util.h"


#define MAX_TAG_STACK_SIZE   16384			// max stack size

char*   perl_attr_check_sub         = "qqq";        // perl method, who check the attribute
char*   perl_make_hrefs_sub         = "www";        // perl method, who make hrefs and extract links
char*   perl_text_check_sub         = "eee";        // perl method, who process the text
char*   perl_plain_text_check_sub   = "rrr";    // perl method, who process the plain text

/*
 *  Check for sequence "\xe2\x80\xa8" (U2028)  and "\xe2\x80\xa9" (U2029)
 *  This utf symbols - IS NOT VALID FOR JAVASCRIPT!
 *  Need replace.
 *
 *  To understand bit masks see wiki utf, for example:  http://ru.wikipedia.org/wiki/UTF-8
 */
#define check_forbidden_utf(pos, start_pos) \
    if( ((unsigned char)*pos & (1 << 7)) && !((unsigned char)*pos & (1 << 6)) \
            && ((unsigned char)*pos == 0xa8 || (unsigned char)*pos == 0xa9) \
            && ((pos - start_pos) > 1) \
            && (unsigned char)*(pos - 1) == 0x80 && (unsigned char)*(pos - 2) == 0xe2 \
      ) \
{ \
    *(pos - 2) = ' '; \
    *(pos - 1) = ' '; \
    *(pos)     = ' '; \
    continue; \
} \


static const char* block_tags[] = {	"address","br","center","dir","div","dl","dt","fieldset","form",	// need to plain_html_text generation
                                    "h1","h2","h3","h4","h5","h6","hr","li","menu","noframes",
                                    "noscript","ol","p","pre","tr","ul"};
static const size_t block_tags_count = sizeof(block_tags) / sizeof(block_tags[0]);


static const char* plain_hr	    = "----------------------------------------------------------------------\n";
static const char* agent_link	= "http://www.mail.ru/agent?message";
static const char* pure_pixel   = "https://img.imgsmail.ru/r/1.gif";

bool is_memc_timeouted	= false;	// set to true, if we have timeout when data received from memcached server
bool ban_img		    = false;	// config parameter. if true, we a getting from memcached info about banned images
bool is_danger_content  = false;	// if in style we found 'expression', 'behavior' e.t.c, set this in true
bool is_corp_user	    = false;	// true, if corp user. defined in MessageProcessor.pm


typedef struct {
    int in_href;       // counter вложенности href'ов
    int in_pre;        // counter вложенности pre'ов
    int in_blockquote; // counter вложенности blockquotes'ов
    int in_div;        // counter вложенности div'ов
    int in_span;       // counter вложенности span'ов
    int in_table;      // counter вложенности table'ов
} tag_nesting_t;

typedef enum {
    E_NONE = 0,
    E_DIV = 1,    // <div>
    E_TABLE,      // <table>
    E_TD         // <td> || <th>
} e_tag_t;

typedef struct {
    e_tag_t etag;
    bool open;
    bool manual;
} stack_tag_t;

typedef struct {
    stack_tag_t tags[MAX_TAG_STACK_SIZE];
    int size;
    bool overflow;
} tstack_t;

static
const char* etag2str(e_tag_t t)
// DEBUG
{
    switch(t)
    {
        case E_NONE: return "none";
        case E_DIV: return "div";
        case E_TABLE: return "table";
        case E_TD: return "td";
    }
    return "unknown";
}

void stack_print(const char *pref, tstack_t *const stack)
{
    printf("%s. stack size: %i\n", pref, stack->size);
    int j = 0;
    for(j = 0; j < stack->size; ++j)
    {
        if(stack->tags[j].open)
            printf("tag[%i] = <%s>\n", j, etag2str(stack->tags[j].etag));
        else
            printf("tag[%i] = </%s>\n", j, etag2str(stack->tags[j].etag));
    }
}

void stack_init(tstack_t *stack)
{
    stack->size = 0;
    stack->overflow = false;
    stack->tags[0].etag = stack->tags[1].etag = E_NONE;
    stack->tags[0].manual = stack->tags[1].manual = false;
}

__attribute__ ((always_inline))
static void check_inner_nesting(tag_nesting_t *n)
{
    if(n->in_div < 0)
        n->in_div = 0;
}

const stack_tag_t* const found_tag_in_current_table(tstack_t *stack, e_tag_t etag, bool open, tag_nesting_t *nesting)
{
    int i = stack->size - 1;
    for( ; i >= 0; --i)
    {
        // Дальше открытой таблицы не ищем
        if(stack->tags[i].etag == E_TABLE && stack->tags[i].open)
        {
            break;
        }
        else if (stack->tags[i].etag == E_DIV)
        {
            if(stack->tags[i].open) ++nesting->in_div;
            else --nesting->in_div;
        }

        if(stack->tags[i].etag == etag && stack->tags[i].open == open)
        {
            check_inner_nesting(nesting);
            return &stack->tags[i];
        }
    }

    check_inner_nesting(nesting);
    return NULL;
}

const stack_tag_t* const stack_back(tstack_t *stack)
{
    int index = stack->size - 1;
    if(index < 0) return NULL;
    return &stack->tags[index];
}


//__attribute__ ((always_inline))
void stack_pop_table(tstack_t *stack, tag_nesting_t *nesting)
{
    int i = stack->size - 1;
    bool found = false;
    for( ; i >= 0; --i)
    {
        if(stack->tags[i].etag == E_TABLE && stack->tags[i].open)
        {
            found = true;
        }
        else if (stack->tags[i].etag == E_DIV)
        {
            if(stack->tags[i].open) ++nesting->in_div;
            else --nesting->in_div;
        }

        stack->tags[i].etag = E_NONE;
        stack->tags[i].open = false;
        stack->tags[i].manual = false;
        --stack->size;

        if(found)
            return;
    }
}

void stack_pop_back(tstack_t *stack)
{
    int index = stack->size - 1;
    if(index < 0) return;
    stack->tags[index].etag = E_NONE;
    stack->tags[index].open = false;
    stack->tags[index].manual = false;
    --stack->size;
}

//__attribute__ ((always_inline))
bool stack_push_manual(tstack_t *stack, e_tag_t etag, bool open)
{
    if( (stack->size + 1) > MAX_TAG_STACK_SIZE )
        return false;

    stack->tags[stack->size].etag = etag;
    stack->tags[stack->size].open = open? true : false;
    stack->tags[stack->size].manual = true;
    ++stack->size;
    return true;
}

bool stack_push(tstack_t *stack, stack_tag_t *tag, tag_nesting_t *nesting, mpop_string *content)
{
    if( (stack->size + 1) > MAX_TAG_STACK_SIZE )
    {
        printf("XS Error: max stack size exceeded\n");
        stack->overflow = true;
        return false;
    }

    bool is_table = (tag->etag == E_TABLE);
    bool is_div = (tag->etag == E_DIV);

    if(tag->open)
    {
        if(is_div) ++nesting->in_div;
        else if(is_table) ++nesting->in_table;

        // Если вставляем <div>, находясь в таблице <table>, то надо сделать следующее:
        // 1) Пройтись по стеку до начала таблицы и поискать <th> или <td>
        // 2) Если не нашли, то добавить <td> перед <div>
        if(is_div && nesting->in_table > 0)
        {
            tag_nesting_t inner_nesting;        // <-- not used => not inited
            if(!found_tag_in_current_table(stack, E_TD, true, &inner_nesting))
            {
                stack_push_manual(stack, E_TD, true);
                add_stringn(content, "<td>", 4);
            }
        }

        stack->tags[stack->size].etag = tag->etag;
        stack->tags[stack->size].open = true;
        stack->tags[stack->size].manual = false;
        ++stack->size;
        return true;
    }

    // Нельзя закрывать неоткрытые таблицы и дивы
    if((is_table && nesting->in_table <= 0) || (is_div && nesting->in_div <= 0))
        return false;

    if(is_div)
    {
        --nesting->in_div;
        const stack_tag_t *t = stack_back(stack);
        // В стеке не нужно держать "схлопывающиеся" дивы, так как они
        // беcполезны, но способны замедлить
        if(t && t->etag == E_DIV && t->open)
        {
            stack_pop_back(stack);
            return true;
        }
    }
    else if(is_table)
    {
        tag_nesting_t inner_nesting;
        inner_nesting.in_div = 0;

        --nesting->in_table;

        const stack_tag_t *t = found_tag_in_current_table(stack, E_TD, true, &inner_nesting);
        if(t && t->manual)
        {
            // Если мы вставили <td> в таблицу, мы же его и закроем
            stack_push_manual(stack, E_TD, false);
            add_stringn(content, "</td>", 5);
        }

        stack_pop_table(stack, &inner_nesting);

        // Когда закрывается таблица, необходимо
        // из общего количества div'ов вычесть количество незакрытых div'ов в таблице
        // * see https://jira.mail.ru/browse/MAIL-22296 - "Foster parenting"
        nesting->in_div -= inner_nesting.in_div;
        return true;
    }

    stack->tags[stack->size].etag = tag->etag;
    stack->tags[stack->size].open = false;
    stack->tags[stack->size].manual = false;
    ++stack->size;
    return true;
}

bool stack_push_id(tstack_t *stack, e_tag_t etag, bool open, tag_nesting_t *nesting, mpop_string *content)
{
    stack_tag_t t;
    t.etag = etag;
    t.open = open;
    t.manual = false;
    return stack_push(stack, &t, nesting, content);
}

static void xtext_list_insert(xtext_list_t *list, char const *key, size_t pos, size_t v_len)
{
    xtext_t* item = (xtext_t*)malloc(sizeof(xtext_t));

    item->next = NULL;
    strncpy(item->memc_key, key, kMemcKeyLen);
    item->memc_key[kMemcKeyLen] = '\0';
    item->position = pos;
    item->len  = v_len;

    if(!list->first)
    {
        list->first = list->last = item;
    }
    else
    {
        list->last->next = item;
        list->last = item;
    }
    ++list->count;
    list->total_len += v_len + 1;				// + пробел
}

static void free_xtext_list(xtext_list_t *list) {
    xtext_t* xt = list->first;
    while(xt)
    {
        xtext_t* tmp = xt;
        xt = xt->next;
        free(tmp);
    }
    memset(list, 0, sizeof(xtext_list_t));
}

typedef enum {
  IS_OKTOTORP,				// link - is anchor, beginning from '#'
  IS_AGENT,				    // link on agent [http://www.mail.ru/agent?message]
  IS_OTHER				    // other link
} link_t;


static inline bool is_danger_style_property(const char* str)
{
    // Енкодами можно закрывать XSS. Например, "expression" = \65\78\70\72\65\73\73\69\6f
    /* very danger is: expression, \@import, \-moz\-binding, script, behavior 		*/

    if(	strcasestr(str, "expression")	||
            strcasestr(str, "@import") 	||
            strcasestr(str, "-moz-binding")	||
            strcasestr(str, "script")	||
            strcasestr(str, "behavior")	||
            strcasestr(str, "-o-link") ||
            strcasestr(str, "fromCharCode")
      )
    {
        is_danger_content = true;
        return true;
    }

    return false;
}

static inline bool has_forbidden_reference(const char *str, const char **_black_ref_list, size_t _b_ref_list_size)
/**
    Вырезаем все ссылки на картинки типа <img src='http://e.mail.ru*'>
    Смотри таск: https://jira.mail.ru/browse/MAIL-15125
    [Если в атрибутах src и стиле background-url есть значения
    "http?s://(swa|swas|auth|e|m|tel|touch|win)\.(mail.ru|bk.ru|inbox.ru|list.ru|my.com).*",
    то эти атрибуты надо вырезать.]
*/
{
    const char *tmp;
    const char *cursor = str;
    static char domain_test[128];

    while((tmp = strstr(cursor, "//")) != NULL)
    {
        int counter = 0;
        const char *begin_link = NULL;
        ptrdiff_t delta = tmp - cursor;

        if ( (delta >= 5 && strncasecmp(tmp - 5, "http:", 5) == 0)  ||          // all http
             (delta >= 6 && strncasecmp(tmp - 6, "https:", 6) == 0) ||          // all https
             (delta == 0) ||                                                    // all <img src = ' //auth.dev.mail.ru  ' >
             (delta >= 3 && ( *(tmp - 1) == ' ' || *(tmp - 1 ) == '('  ))       // all <div background: url ( //auth.dev.mail.ru)  >
           )
        {
            begin_link = tmp + 2;
        }
        else
        {
            cursor = tmp + 2;
            continue;
        }

        memset(domain_test, 0, 128);
        cursor = begin_link;

        while( (isalnum(*begin_link) || *begin_link == '.') && counter < 127 )
        {
            domain_test[counter] = tolower(*begin_link);
            ++begin_link;
            ++counter;
        }

        {
            char* pc = &domain_test[0];
            char** found_it = (char**)bsearch( (char*)&pc, _black_ref_list, _b_ref_list_size, sizeof(char*), mb_compare_string_p);
            if( found_it ) {
                return true;
            }
        }
    }
    return false;
}

static int call_perl_attribute_checker(const char* method, const char* tag, const char* attr, const char* val, char* res, char quote, const char* form_method)
{
    strncpy(res, &quote, 1);
    strcat(res, val);
    strncpy(res, &quote, 1);
    return 1;
}

int HtmlParser::url_in_style_process(char* str, const char** _black_ref_list, size_t _b_ref_list_size)
{
    // TODO: str - is no url!!! is just may be contained some url
    m_Links.push_back(str);

    const char* buf;
    static char from_perl_buf[kMaxAttrValueLen];
    size_t fpb_len;

    if(has_forbidden_reference(str, _black_ref_list, _b_ref_list_size))
    {
        str[0] = '\0';
        return 0;
    }

    memset(from_perl_buf, 0, kMaxAttrValueLen);
    call_perl_attribute_checker(perl_attr_check_sub, "dontcare", "style", str, from_perl_buf, ' ', "dontcare");

    buf = from_perl_buf;
    while( *buf == ' ') ++buf;
    fpb_len = strlen(buf);
    memcpy(str, buf, fpb_len );
    str[fpb_len] = '\0';
    return 0;
}

static inline bool is_negative_val(const char* value)
/*
 *  true, if is negative value in style property.
 *  for example: text-indent: -18.0pt;
 */
{
    const char* ptr = value;
    while(ptr && *ptr && isspace(*ptr)) ++ptr;
    return *ptr == '-';
}

static inline bool is_negative_full_val(const char * value)
/*
 *  Is using in n case:
 *  margin: 0px;
 *  margin: 0px 0px;
 *  margin: 0px 0px 0px;
 *  margin: 0px 0px 0px 0px;
 *
 *  padding: 0px;
 *  padding: 0px 0px;
 *  padding: 0px 0px 0px;
 *  padding: 0px 0px 0px 0px;
*/
{
    const char* ptr = value;
    while(ptr && *ptr)
    {
        if(*ptr == '-') return true;
        ++ptr;
    }
    return false;
}

static inline bool is_ignored_style_property(const char* property, const char* value)
/*
 *  just ignoring this property in style. no warnings, no panic.
 */
{
    if( strcmp(property, "position")==0 || strcmp(property, "left")==0 || strcmp(property, "top")==0 ||
        strcmp(property, "z-index")==0 || (strcmp(property, "margin-left")==0 && is_negative_val(value)) || (strcmp(property, "margin-top")==0 && is_negative_val(value)) ||
        (strcmp(property, "margin") == 0 && is_negative_full_val(value)) || (strcmp(property, "padding") == 0 && is_negative_full_val(value)) ||
        (strcmp(property, "text-indent")==0 && is_negative_val(value)) ||
        strcmp(property, "transform")==0 || strcmp(property, "-webkit-transform")==0 || strcmp(property, "-moz-transform")==0 ||
        strcmp(property, "-o-transform")==0 || strcmp(property, "-ms-transform")==0
        || strcmp(property, "zoom")==0 || strcmp(property, "opacity")==0 || strcmp(property, "-khtml-opacity")==0 || strcmp(property, "-moz-opacity")==0
        || (   (strcmp(property, "filter")==0 || strcmp(property, "-ms-filter")==0) && *value && strcasestr(value, "opacity")    )

      )
    {
        // cut this property
        return true;
    }

    return false;
}


#define style_add_prop() \
/*warn("prop: %s, val: %s", property, p_value);*/ \
if(!has_utf && !is_ignored_style_property(property, p_value)) \
{ \
    if(is_danger_style_property(p_value)) return -1; \
    if( has_url ) url_in_style_process(p_value, _black_ref_list, _b_ref_list_size); \
    p_len = strlen(property); \
    pv_len = strlen(p_value); \
    if(p_len && pv_len) \
    { \
        strcat(_res, property); \
        strncat(_res, ":", 1); \
        strcat(_res, p_value); \
        ++p_count; \
    } \
} \

int HtmlParser::check_style(const char* _str, int _len, char _external_quote,
                                    char* inner_quot, char*  _res, const char** _black_ref_list,
                                    size_t _b_ref_list_size)
/*
    _str - is style attribute value
    _len - size of value
    external_quote - the quotes type around style attribute value
    return: count of parsed style properties
*/
{
    bool is_property = true;                            // стиль начинается с описания какого-либо свойства
    const char* cursor = _str;

    bool has_quot = false;
    bool has_double_quot = false;
    bool has_utf = false;

    int test_value_buffer_counter = 0;
    int p_value_buffer_counter = 0;
    int property_buffer_counter = 0;
    int all_style_chars_counter = 0;

    bool in_quotes = false;                             // true, если значение свойства заключено в кавычки ', " или ()
    bool has_url = false;                               // true для свойств типа background: url()
    char quote;
    int p_count = 0;                                    // количество найденных свойств

    static char property[kMaxTagLen];                   // сюда помещается название свойства
    static char p_value[kMaxAttrValueLen];              // <-- значение свойства
    static char test_value_buffer[kMaxAttrValueLen];    // <-- стиль полностью , но без пробелов и т.п., чтобы в нем можно было искать "опасные" behavior e,t.c.

    memset(property, 0, sizeof(property));
    memset(p_value, 0, sizeof(p_value));
    memset(test_value_buffer, 0, _len + 1);

    do
    {
        char cur_char;
        bool is_encode = false;

        if(strncmp(cursor,"/*",2) == 0 ) 			// comments in style: <img style='xss:expr/*XSS*/ession....'>
        {
            char* pch = strstr(cursor, "*/");
            if(pch)
                cursor = pch + 1;
            else
                return -1;
        }

        if( *cursor == '\\')					// 16-ричный код
        {
            char code[3];
            int code_counter = 0;
            const char *tmp = cursor;               // запомнили позицию, чтобы откатиться, если поймем что это не енкод

            while(*cursor == '\\' && *cursor)		// пропустили style='\\\0065\\0078'
                ++cursor;

            if(ispunct(*cursor) && ((cursor - tmp) % 2 == 1))
            {
                // экранированая кавычка, пропускаем
                printf("XS Error: escaped punct symbol detected: '%c' (style ignored: %s)\n", (unsigned char)*cursor, _str);
                return -1;
            }

            while(*cursor == '0' && *cursor)		// пропустили style='\\\000065'
                ++cursor;

            while( isxdigit(*cursor) && code_counter < 2 )	// максимально символов на один енкод = 2, например, вместо \\00065 рассматриваем \65, \\64ehavior -> (\64)ehavior
            {
                code[code_counter] = *cursor;
                ++code_counter;
                ++cursor;
            }
            code[code_counter] = '\0';

            if(code_counter > 0)
            {
                long lcode = strtol(code, 0, 16);
                char decode = (char)lcode;
                cur_char = tolower(decode);
                is_encode = true;
            }
            else
            {
                cursor = tmp;
            }
        }
        /**
            Viki: http://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references.
            "A numeric character reference refers to a character by its Universal Character Set/Unicode code point, and uses the format:
              - &#nnnn;
              - &#xhhhh;
              - &#Xhhhh;
            where 'nnnn' is the code point in decimal form, and 'hhhh' is the code point in hexadecimal form."
         */
        else if ( *cursor == '&')
        {
            const char *tmp = cursor;
            ++cursor;

            if(isalpha(*cursor))
            {
                if(strncasecmp(cursor, "apos", 4) == 0)
                {
                    cur_char = '\'';
                    // ниже есть проверка на '\0'
                    cursor += 4;
                    is_encode = true;
                    if(*cursor == ';')
                        ++cursor;
                }
                else if(strncasecmp(cursor, "quot", 4) == 0)
                {
                    cur_char = '\"';
                    // ниже есть проверка на '\0'
                    cursor += 4;
                    is_encode = true;
                    if(*cursor == ';')
                        ++cursor;
                }
                else if(strncasecmp(cursor, "lpar", 4) == 0)
                {
                    cur_char = '(';
                    // ниже есть проверка на '\0'
                    cursor += 4;
                    is_encode = true;
                    if(*cursor == ';')
                        ++cursor;
                }
                else if(strncasecmp(cursor, "bsol", 4) == 0)
                {
                    cur_char = '\\';
                    // ниже есть проверка на '\0'
                    cursor += 4;
                    is_encode = true;
                    if(*cursor == ';')
                        ++cursor;
                }
                else
                {
                    cursor = tmp;
                }
            }
            else
            {
                // символ '#' может быть заенкожен
                while (*cursor == '\\' && *cursor) ++cursor;

                if(*cursor == '#')
                {
                    bool is_hex = false;
                    char code[64];
                    int code_counter = 0;

                    ++cursor;

                    if(*cursor == 'x' || *cursor == 'X')
                    {
                        is_hex = true;
                        ++cursor;
                    }

                    while( code_counter < 64)
                    {
                        if(is_hex) {if (!isxdigit(*cursor)) break;}
                        else {if (!isdigit(*cursor)) break;}

                        code[code_counter] = *cursor;
                        ++code_counter;
                        ++cursor;
                    }
                    code[code_counter] = '\0';

                    if(code_counter > 0)
                    {
                        long lcode = strtol(code, 0, is_hex? 16 : 10);
                        char decode = (char)lcode;
                        cur_char = tolower(decode);
                        is_encode = true;
                        if(*cursor == ';') ++cursor;                       // броузеры воспринимают ';' после 10-чного html-кода как часть кодированного символа
                    }
                    else
                    {
                        cursor = tmp;
                    }
                }
                else
                {
                    cursor = tmp;
                }
            }
        }
        else if ( (int)((unsigned char)(*cursor) >> 6) == 3)
        {
            // yandex и google полностью вырезают no utf symbols из стилей
            has_utf = true;
        }
        else if((iscntrl(*cursor) && !isspace(*cursor)) || *cursor == 0x0c)
        {
            printf("XS Error: control symbol detected: '%c' (style ignored)\n", (unsigned char)*cursor);
            return -1;
        }

        if(!is_encode) cur_char = *cursor;
        else
        {
            // valid encoded chars just: ', ", and space
            if(cur_char != '\'' && cur_char != '\"' && cur_char != ' ') // XSS on MAIL-12488
            {
                printf("XS Error: wrong encoded char: '%c' [valid just ', \" and space]\n", cur_char);
                return -1;
            }
            if (cur_char == _external_quote)    // XSS on MAIL-12017
            {
                //warn("XS Error: the same quotes in style as external quotes. style ignored");
                //return -1;
                if(cur_char != ' ')
                {
                    char bad_quote = cur_char;
                    cur_char = (_external_quote == '\'')? '\"' : '\'';
                    //warn("XS Error: the same quotes in style as external quotes. change '%c' --> '%c'", bad_quote, cur_char);
                }
                else
                {
                    // Пробелы можно никак не трогать, так как все атрибуты принудительно оковычиваются
                }
            }
        }

        if(cur_char == '\0') continue;

        // заполнение буфера, который потом будет анализироваться на script e.t.c.
        if(isalpha(cur_char) || cur_char == '@' || cur_char == '-')
        {
            test_value_buffer[test_value_buffer_counter] = cur_char;
            ++test_value_buffer_counter;
        }

        if(cur_char == ':')
        {
            if(is_property)
            {
                is_property = false;
                if(!is_encode) ++cursor;
                continue;
            }
        }

        if(is_property)
        {
            cur_char = tolower(cur_char);
            if(isalpha(cur_char) || cur_char=='-' )						// css свойства могут состоять только из букв и знака '-'
            {
                property[property_buffer_counter] = cur_char;
                ++property_buffer_counter;
                if(property_buffer_counter >= kMaxTagLen)
                {
                    property[kMaxTagLen - 1] = '\0';
                    printf("XS Error: too large css property: %s (style ignored)\n", property);
                    return -1;
                }
            }
            if(!is_encode) ++cursor;
        }
        else
        {
            if(in_quotes && quote == ')')
            {
                long code = (long)cur_char;
                if(has_url && ((code >= 1 && code <= 32) || code == 40 || code == 127 || code == 160))
                {
                    char hex_code[9] = {0};
                    hex_code[0] = '\\';
                    sprintf(hex_code + 1, "%06x", code);
                    hex_code[7] = ' ';
                    strncpy(p_value + p_value_buffer_counter, hex_code, 8);
                    p_value_buffer_counter += 8;
                }
                else
                {
                    p_value[p_value_buffer_counter] = cur_char;
                    ++p_value_buffer_counter;
                }
                if(cur_char == '"') { has_double_quot = true; *inner_quot = '"'; }
                else if(cur_char == '\'') { has_quot = true; *inner_quot = '\''; }
            }
            else
            {
                if( cur_char != '\t' && cur_char != '\n')
                {
                    p_value[p_value_buffer_counter] = cur_char;
                    ++p_value_buffer_counter;
                    if(cur_char == '"') { has_double_quot = true; *inner_quot = '"'; }
                    else if(cur_char == '\'') { has_quot = true; *inner_quot = '\''; }
                }
                else //replacing '\t' and '\n' with space
                {
                    p_value[p_value_buffer_counter] = ' ';
                    ++p_value_buffer_counter;
                }
            }

            if(p_value_buffer_counter >= kMaxAttrValueLen)
            {
                p_value[kMaxAttrValueLen - 1] = '\0';
                printf("XS Error: too large css value: %s (style ignored)\n", p_value);
                return -1;
            }

            if( !in_quotes && (cur_char == '"' || cur_char == '\'' || cur_char == '(') )
            {
                in_quotes = true;
                quote = (cur_char == '(')? ')' : cur_char;
            }
            else if(in_quotes && cur_char == quote )
            {
                in_quotes = false;
            }

            if(cur_char == '(' && !has_url && strcasestr(p_value, "url"))
                has_url = true;

            if( !in_quotes && (cur_char == ';' || cur_char == '}') )
            {
                int p_len = property_buffer_counter, pv_len = p_value_buffer_counter;
                style_add_prop();

                memset(property, 0, p_len + 1);
                memset(p_value, 0, pv_len + 1);
                is_property = true;
                has_utf = false;
                has_url = false;
                all_style_chars_counter += (p_len + pv_len + 2);
                if(all_style_chars_counter >= kMaxAttrValueLen)
                {
                    _res[kMaxAttrValueLen - 1] = '\0';
                    printf("XS Error: too large style - ignored\n");
                    return -1;
                }

                p_value_buffer_counter = 0;
                property_buffer_counter = 0;
            }

            if(!is_encode) ++cursor;

            if(!*cursor && *property)											// если style = '123:456'  не заканчивается на ';'
            {
                int p_len = property_buffer_counter, pv_len = p_value_buffer_counter;
                if(in_quotes)
                {
                    printf("XS Error: no quot closed value: %s\n", p_value);
                    return 0;
                }

                style_add_prop();
                all_style_chars_counter += (p_len + pv_len + 2);
                if(all_style_chars_counter >= kMaxAttrValueLen)
                {
                    _res[kMaxAttrValueLen - 1] = '\0';
                    printf("XS Error: too large style - ignored\n");
                    return -1;
                }
            }
        }
    }
    while(*cursor);

    if(p_count > 0 && is_danger_style_property(test_value_buffer)) return -1;
    if(has_quot && has_double_quot)
    {
        printf("XS Error: both quoted style - forbidden [style: %s]\n", _str);
        return -1;
    }

    return p_count;
}

static inline bool has_web_protocol(const char* str, int len)
/*
    return true, if attribute value consider some protocol.
*/
{
    if( (len > 1) && str[0] == '/' && str[1] == '/') return true;

    if(strcasestr(str,"www")) 	return true;

    if(strcasestr(str,"http://"))	return true;
    if(strcasestr(str,"https://"))	return true;
    if(strcasestr(str,"ftp://"))	return true;
    if(strcasestr(str,"afs://"))	return true;
    if(strcasestr(str,"wais://"))	return true;
    if(strcasestr(str,"telnet://")) return true;
    if(strcasestr(str,"gopher://")) return true;

    if(strcasestr(str,"news:"))	return true;
    if(strcasestr(str,"nntp:"))	return true;
    if(strcasestr(str,"mid:"))	return true;
    if(strcasestr(str,"cid:"))	return true;
    if(strcasestr(str,"skype:"))	return true;
    if(strcasestr(str,"prospero:")) return true;
    if(strcasestr(str,"mailto:"))	return true;

    return false;
}

static inline bool is_url(const char* str, int len)
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

static inline bool is_begin_from_url(const char* str, int len)
/*
    found strings like '{{http://www.ya.ru}}'
*/
{
    const char* tmp;
    if( (len > 1) && str[0] == '/' && str[1] == '/') return true;

    tmp = str;
    while (*tmp && !isalpha(*tmp)) {
        ++tmp;
        --len;
    }
    return is_url(tmp, len);
}

static int call_perl_text_checker(const char* method, const char* _begin, size_t _len, char** _res)
/*
    perl call for text or plain-text process.
    1) if in text we finding some protocols;
    2) if in plain-text we finding some entities.
*/
{
    std::string tmp(_begin, _len);
    *_res = strdup(tmp.c_str());
    return 1;
}


static inline void html_text_process(char* _begin, size_t _len, tag_nesting_t const *nesting, mpop_string* _res)
/*
    if in html text some protocols found, perl make hrefs calling here.
    e.g. www.yandex.ru --> <a href="ololo://www.yandex.ru"> www.yandex.ru </a>
*/
{
    char save_me;
    char* ch_text = 0;
    bool has_phones = false;
    mpop_string html_text;

    if(_len <= 0) return;
    save_me = _begin[_len];
    _begin[_len] = '\0';

    if(!nesting->in_href)
    {
        do {
            int non_space_symbols = 0;
            char* tmp;

            if(_len < 5) break;

            tmp = _begin;
            while(*tmp)
            {
                if(isalnum(*tmp)) ++non_space_symbols;
                if(non_space_symbols > 5) break;
                ++tmp;
            }

            if(non_space_symbols < 5) break;
            if(!has_web_protocol(_begin,_len)) break;
            call_perl_text_checker(perl_text_check_sub, _begin, _len, (char**)&ch_text);		// <-- make hrefs here
        }
        while(0);
    }

    init_string(&html_text);

    {   // check on phone numbers

        typedef enum {PLUS, SPACE, DIGIT, OTHER} ps_t;
        char* tmp = (ch_text)? ch_text : _begin;
        char* plus;

        while(plus = strchr(tmp, '+'))
        {
            char* cursor = plus;
            ps_t state = PLUS;
            int phone_len = 0;

            add_stringn(&html_text, tmp, plus - tmp);

            while(1)
            {
                bool need_break = false;
                switch(state){
                    case PLUS: {
                                   ++cursor;
                                   //if(*cursor == '-' || *cursor == ' ' || *cursor == '(' || *cursor == ')' || *cursor == '.' || *cursor == '/') state = SPACE;
                                   //if(*cursor == '-' || *cursor == '(' || *cursor == ')' || *cursor == '.' || *cursor == '/') state = SPACE;
                                   if(0) {}
                                   else if ( isdigit(*cursor) ) state = DIGIT;
                                   else state = OTHER;
                                   break;
                               }
                    case SPACE: {
                                    char prev_space = *cursor;
                                    ++cursor;
                                    if(*cursor == prev_space) {
                                        state = OTHER;
                                        break;
                                    }
                                    if( (*cursor == '-' || *cursor == '(' || *cursor == ')' || *cursor == '.' || *cursor == '/') && prev_space != ' ' ) {
                                        state = OTHER;
                                        break;
                                    }
                                    if(*cursor == '-' || *cursor == ' ' || *cursor == '(' || *cursor == ')' || *cursor == '.' || *cursor == '/') state = SPACE;
                                    else if ( isdigit(*cursor) ) state = DIGIT;
                                    else state = OTHER;
                                    break;
                                }
                    case DIGIT: {
                                    ++phone_len;
                                    ++cursor;
                                    if(*cursor == '-' || *cursor == ' ' || *cursor == '(' || *cursor == ')' || *cursor == '.' || *cursor == '/') state = SPACE;
                                    else if ( isdigit(*cursor) ) state = DIGIT;
                                    else state = OTHER;
                                    break;
                                }
                    case OTHER: {
                                    if ( phone_len >= 7 && phone_len <= 15)
                                    {
                                        static char phone[32];
                                        memset(phone, 0, 32);
                                        strncpy(phone, plus, cursor - plus);
                                        add_string(&html_text, "<span class=\"js-phone-number\">");
                                        add_string(&html_text, phone);
                                        add_string(&html_text, "</span>");
                                        has_phones = true;
                                    }
                                    else
                                    {
                                        add_stringn(&html_text, plus, cursor - plus);
                                    }
                                    need_break = true;
                                }

                }
                if( need_break ) {
                    tmp = cursor;
                    break;
                }
            }   // while(1)
        }       // while(plus = strchr(tmp, '+'))

        add_string(&html_text, tmp);

    }           // check on phone numbers

    if(ch_text)
    {
        if(has_phones)
            add_stringn(_res, html_text.string, html_text.size);
        else
            add_string(_res, ch_text);

        free(ch_text);
    }
    else
    {
        add_stringn(_res, has_phones? html_text.string : _begin, has_phones? html_text.size : _len);
    }

    free_string(&html_text);
    _begin[_len] = save_me;
}



static inline void add_blockquote_to_plain(int _in_blockquote, mpop_string* _res)
/*
    in plain buffer text tag <blockquote> --> ">>>"
*/
{
    int i;
    bool not_need = true;

    if(_res->size >= _in_blockquote){
        for(i = 0; i < _in_blockquote; ++i){
            if( (_res->size - 1 - i) >= 0 && _res->string[_res->size - 1 - i] != '>') {
                not_need = false;
                break;
            }
        }
    }

    if(not_need) return;

    for(i = 0; i < _in_blockquote; ++i)
        add_stringn(_res, ">", 1);
}


static inline void plain_text_process(char* _begin, size_t _len, tag_nesting_t const *nesting, mpop_string* _res)
/*
    process plain buffer.
    1) many "\n\n\n" in one "\n"
    2) add blockqutes ">>>", if needed
    3) entities decoded. e.g. &amp; --> '&'
*/
{
    char save_me;
    char* tmp;
    int real_len;
    char* ch_text = 0;
    int non_spaces = 0;
    int i;

    if(_len <= 0) return;

    save_me = _begin[_len];
    _begin[_len] = '\0';

    tmp = _begin;
    real_len = _len;

    if(!nesting->in_pre)
    {
        char* end;
        char last_cutted_symbol;

        for(i = 0; i < _len; ++i) {
            if (!isspace(*tmp)) {
                ++non_spaces;
                break;
            }
            ++tmp;
            --real_len;
        }
        if (non_spaces == 0) {                                      // strings who contain only spaces not process
            _begin[_len] = save_me;
            return;
        }

        if( _res->size == 0 || _res->string[_res->size - 1] != '\n') {                 // for example, <br> --> '\n'
            add_char(_res, ' ');                                    // XXX: change any space-symbol by space ?

            //--tmp;                                                // XXX: or save the last original space symbol ?
            //++real_len;
        }

        end = _begin + _len - 1;
        last_cutted_symbol = '0';					                // последний пробел все же надо сохранить

        while( end != tmp )
        {
            if(real_len == 0) break;

            if( *end == '\n' || *end == '\r' || *end == ' ')
                --real_len;
            else
                break;

            last_cutted_symbol = *end;
            --end;
        }
        if (last_cutted_symbol == ' ') ++real_len;
    }

    if( _res->size > 0 && _res->string[_res->size - 1] == '\n')
        if(nesting->in_blockquote) add_blockquote_to_plain(nesting->in_blockquote, _res);

    do {
        bool has_amp 	= false;
        char* amp_position = 0;
        bool has_entity = false;
        char* t;

        if(real_len < 2) break;

        t = tmp;
        while(*t)
        {
            if(*t == '&')
            {
                has_amp = true;
                amp_position = t;
            }

            if(*t == ';' && has_amp && (t - amp_position) < 10 )
            {
                has_entity = true;
                break;
            }
            ++t;
        }

        if(!has_entity) break;
        call_perl_text_checker(perl_plain_text_check_sub, tmp, real_len, (char**)&ch_text);			// <-- html entities decodes here
    }
    while(0);

    if(ch_text)
    {
        add_string(_res, ch_text);
        free(ch_text);
    }
    else
    {
        add_stringn(_res, tmp, real_len);
    }

    _begin[_len] = save_me;
}


static inline int xss_is_tag(const char* _begin, const char* _end)
/**
 * @brief true, если кусок текста, заключенный в угловые скобки, является тегом.
 *
 * "Имя представляет собой последовательность символов, содержащую только буквы английского алфавита (A - Z, a - z), цифры (0 - 9), промежутки времени, дефисы (-)
 * и начинающуюся с буквы." (c)
 *
 * Пустые <> - не тег
 * Если есть пробел: < tag> - не тег
 * Если начинается с цифры, <1tag> - не тег
 *
 *
 * @param _begin '<'
 * @param _end следующий за '>'
 * @return -1 - не тег, 0 - тег, 1 - <!doctype ...>
 **/
{
    const char* tmp = _begin + 1;

    if (*tmp == '/') ++tmp;			// все закрывающие теги [ </script>, </table> etc]

    if( isalpha(*tmp))
        return 0;

    if ( *tmp == ' ' || *tmp == '>' || *tmp == '\t')
        return -1;

    if( *tmp == '!')	// !doctype and xml
    {
        const char* end;

        if(strncasecmp(tmp, "!doctype", 8) == 0)
            return 1;					// возможно !doctype и другие теги на ! придется обрабатывать отдельно, поэтому return 1

        end = _end - 1;						// end смотрит на закрывающий '>'

        do { --end;}  while(isspace(*end));

        if (*(++tmp) == '[' && *end == ']')		// xml constructions like <![if !supportLists]>, <![if !vml]>, <![endif]>,
          return 1;
    }
    else if( *tmp == '?')				// fuck'n xml like '<?xml version='1.0' encoding='UTF-8'?>' or <?xml:namespace prefix = o />
    {
        if(strncasecmp(tmp, "?xml", 4) == 0)
            return 1;					// возможно ?xml придется обрабатывать отдельно, поэтому return 1
    }

    return -1;
}


static inline bool xss_is_white_tag(tag_t const *tag, const char** _white_list, size_t _wlist_size)
/**
 * @brief true, если тег в белом списке
 *
 * если пришел тег /body, то проверяется только body, то есть символ '/' у тегов отрезается.
 * тег ожидается в нижнем регистре.
 *
 * @param tag	- структура с тэгом
 * @return bool
 **/
{
    char const *bp = tag->name;
    if (*bp == '\0') return false;
    return bsearch(&bp, _white_list, _wlist_size, sizeof(char*), mb_compare_string_p);
}


static inline bool xss_cut_comments(char const * _ptr, char const ** _cut_it)
/**
 * @brief Идентификация комментариев.
 *
 * @param _ptr указатель на начало проверяемого текста
 * @param _cut_it сюда будет помещен конец вырезаемого текста или NULL, если конец не найден
 * @return bool true, если надо вырезать
 **/
{
    const char comment_begin[]        = "<!--";
    const char comment_end[]          = "-->";
    const size_t c_len		          = sizeof(comment_begin) - 1;

    char const * begin = _ptr;

    // проверка на коммент
    if(strncmp(comment_begin, begin, c_len ) == 0)
    {
        char const * end = strstr(begin, comment_end);
        if(end)							// пропускаем комментарий
        {
            end += sizeof(comment_end) - 1;			// end - конец комментария
            *_cut_it = end;
            return true;
        }
        return true;						// _cut_it = NULL
    }
    return false;
}

static bool xss_get_tag(char const *_ptr, tag_t* tag)
/**
 * @brief Получение имени тэга по адресу _ptr.

 * @param _ptr указатель на начало проверяемого текста (на '<')
 * @return bool true, если тэг корректный.
 **/
{
    size_t counter;
    char const *cursor = _ptr + 1; // сдвинули cursor с '<'

    // all tags replaced by tolower(tags) now!
    tag->open = true;

    if(*cursor == '/')
    {
        ++cursor;
        tag->open = false;
    }

    counter = 0;
    while(cursor[counter] != '>' && !isspace(cursor[counter]))
    {
        tag->name[counter] = tolower(cursor[counter]);
        *((char *)cursor + counter) = tag->name[counter]; // FIXME: меняет изначальную строку, чтобы полученный текст был в нижнем регистре
        ++counter;

        if(counter == kMaxTagLen)
        {
            tag->name[counter-1] = '\0';
            printf("XS Error: too long tag name:%s. max tag name size = %i\n", tag->name, kMaxTagLen);
            return false;			// слишком длинный тег не войдет в список белых тегов и будет вырезан
        }
    }
    if (counter && cursor[counter - 1] == '/') --counter; // handle <br/>, <li/> and so on
    tag->name[counter] = '\0';
    return true;
}

static bool xss_cut_some_tags(char const * _ptr, char const ** _cut_it, mpop_string* _title, tag_t const *tag)
/**
 * @brief Проверка участка текста на "вырезаемость". Полностью вместе с внутренностями вырезаются теги: script, style, title.

 * @param _ptr указатель на начало проверяемого текста (на '<')
 * @param _cut_it сюда будет помещен конец вырезаемого текста или NULL, если конец не найден
 * @param _title сюда будет помещен title, если будет найден.
 * @return bool true, если надо вырезать
 **/
{
    char const *begin = _ptr;

    if (!tag->open) return false;			// закрывающие теги сами по себе не вырезаются

    // проверка на <title>. сам title сохраняется.
    if(strncmp("title", tag->name, 5 )  == 0)
    {
        char const *end;
        while(begin && *begin != '>') ++begin;
        end = strcasestr(begin, "</title");
        if(end)
        {
            char const *tmp = end;

            while(end && *end != '>' && *end != '\0') ++end;
            if(end && *end != '\0')
            {
                add_stringn(_title, begin + 1, tmp - begin - 1);	// сохранили title
                *_cut_it = ++end;
            }
            return true;
        }
        return true;						// _cut_it = NULL
    }

    // проверка на <script>. Возможно будет дополняться. Пока тупенько вырезается.
    if(strncmp("script", tag->name, 6 ) == 0)
    {
        char const *end = strcasestr(begin, "</script");
        if(end)
        {
            while(end && *end != '>' && *end != '\0') ++end;
            if(end && *end != '\0')
                *_cut_it = ++end;
            return true;
        }
        return true;						// _cut_it = NULL
    }

    // проверка на <xml>. Тупенько вырезается.
    if(strncmp("xml", tag->name, 3 ) == 0)
    {
        char const *end = strcasestr(begin, "</xml");
        if(end)
        {
            while(end && *end != '>' && *end != '\0') ++end;
            if(end && *end != '\0')
                *_cut_it = ++end;
            return true;
        }
        return true;						// _cut_it = NULL
    }

    // проверка на <style>. style необходимо вырезать правильно - аккуратно обработав комментарии и последовательности {...} внутри style.
    if(strncmp("style", tag->name, 5 ) == 0)
    {
        begin += sizeof("style") - 1;

        for(;*begin && begin; ++begin)
        {
            /*
            UPD: эта логика убивает убогую рассылку от твиттера, поэтому приходится проститься с тобой, валидный CSS !

            if( *begin == '{') 				// пропускаем { ... }.
            {
                int recursive = 0;			// '{' могут быть вложенными

                while(*begin && begin)
                {
                    ++begin;

                    if( *begin == '{')
                      ++recursive;

                    if( *begin == '}')
                    {
                      if(recursive == 0)
                        break;
                      else
                        --recursive;
                    }
                }
            }
            */

            if( *begin == '/' && strncmp(begin, "/*", 2) == 0 )			// пропускаем комментарии
            {
                while(*begin && begin)
                {
                    ++begin;
                    if( *begin == '*' && strncmp(begin, "*/", 2) == 0)
                      break;
                }
            }

            if( *begin == '<' && strncasecmp(begin, "</style", 7) == 0 )
            {
                begin += sizeof("</style") - 1;
                while(begin && *begin != '>' && *begin != '\0' ) ++begin;
                if(begin && *begin != '\0')
                    *_cut_it = ++begin;
                return true;
            }
        }

        return true;						// _cut_it = NULL
    }

    return false;
}



int HtmlParser::xss_attribute_parsing(const char* _begin, const char* _end, tag_t* tag,
                                        const char** _black_list, size_t _b_list_size,
                                        const char** _black_ref_list, size_t _b_ref_list_size,
                                        xtext_list_t* _xtext_list, char* _attr_str)
/**
 * @brief парсинг атрибутов
 *
 * @param _begin указатель на начало строки атрибутов (например, xmlns="http://www.w3.org/1999/xhtml" xml:lang="ru-ru" lang="ru-ru" >)
 * @param _end указатель на конец списка атрибутов (указывает на закрывающий символ >)
 * @param _attr указатель на строку атрибутов
 * @return int  -1, если есть не well formed атрибуты или не удалось распарсить по другим причинам, иначе количество отпарсенных атрибутов.
 **/
{
    const char* left = _begin;
    const char* t = _begin;

    static const char* textarea     = "textarea";       // <textarea>
    static const char* form         = "form";           // form вырезается у тега <textarea>
    static const char* target       = "target";
    static const char* formtarget   = "formtarget";
    static const char* base         = "base";
    static const char* href         = "href";

    link_t href_link = IS_OTHER;

    bool has_target = false;                // true, if tag has 'target' attribute
    bool has_href   = false;                // true, if tag has 'href' attribute
    bool has_action = false;                // true, if attribute == 'action' or 'formaction'

    int count = 0;
    int total_len = 0;

    do
    {
        ++t;
        if( *t == '=')							// нашли какой-то атрибут.
        {
            const char* attr_end;
            const char* attr_begin;
            size_t a_len;
            static char attr[kMaxAttrLen];
            size_t i;
            bool is_black;
            const char* tmp;
            bool is_quote_closed;
            bool is_well_formed;
            bool need_save_quoted_value;
            char quote;

            for(; left != t; ++left) if( isalpha(*left) ) break;	// передвинули left в начало атрибута [*left != ' ', '\t', '\n', '/' e.t.c.]
            if( left == t ) return -1;				            	// если '=' есть, а атрибута нет, то left упирается в t

            attr_end = t;					                        // нужно вырезать пробелы в конце атрибута, чтобы проверить его валидность
            do
            {
                --attr_end;
                if(isalpha(*attr_end) || isdigit(*attr_end) ) break;	// в теге по идее могут быть цифры.

            }while(attr_end != left);					            // attr_end == left, если атрибут из 1 симовла

            attr_begin = attr_end;			            	        // поиск действительного начала атрибута (защита от невалидности типа <table 123 border=5>)
            if(attr_begin != left)
            {
                --attr_begin;
                while(attr_begin != left)
                {
                    if(isspace(*attr_begin))
                    {
                        left = ++attr_begin;			            // now left - is real begin of attribute
                        break;
                    }

                    // Атрибуты могут содержать буквы, цифры, двоеточие и минус.
                    // MAIL-22943, добавляем нижнее подчеркивание
                    if(! ( isalnum(*attr_begin) || *attr_begin == ':' || *attr_begin == '-' || *attr_begin == '_') )
                    {
                        printf("XS Error: not valid attribute in tag %s, bad symbol '%c' [%i] found\n", tag->name, *attr_begin, (int)*attr_begin);
                        return -1;
                    }

                    --attr_begin;
                }
            }

            a_len = attr_end + 1 - left;				            // cut attribute name and tolower it
            if(a_len < 1 || a_len > kMaxAttrLen)
            {
                printf("XS Error: invalid attribute len:%i [max=%i]\n", a_len, kMaxAttrLen);
                return -1;
            }

            attr[a_len] = '\0';
            strncpy(attr, left, a_len);
            for(i = 0; i < a_len; ++i) attr[i] = tolower(attr[i]);	// чтобы сравнивать с "src", атрибут нужен в нижнем регистре

            is_black = false;
            {								                    // проверка атрибута на "черность"
                static const char* on_attributes = "on";		// атрибуты "on*" вырезаются
                char* pc = &attr[0];
                char** found_it = (char**)bsearch( (char*)&pc, _black_list, _b_list_size, sizeof(char*), mb_compare_string_p);

                if( found_it || strncmp(on_attributes, attr, 2 ) == 0 || (strcmp(tag->name,textarea)==0 && strcmp(attr,form)==0 ) )
                    is_black = true;
            }

            ++t;		                						// сдвинули t со знака '='
            for(; *t; ++t) if( !isspace(*t) ) break;			// пропустили все пробелы + '\n' '\t' '\r' etc.
            tmp = t;						                    // tmp и t указывают на начало значения атрибута

            is_quote_closed = true;
            is_well_formed = false;					           // если этот тег не well formed, то значение не изменится
            need_save_quoted_value = false;
            quote = ' ';

            for(; *tmp; ++tmp)							        // теперь парсим значение атрибута
            {
                // 1. если атрибут в кавычках, то считаем, что вторые кавычки - конец атрибута
                if( *tmp == '"' || *tmp == '\'')
                {
                    if( (tmp - t) > 0) is_black = true;				// защита от невалидности типа: <tag attr=123"onload=alert(test) id=" dd">

                    is_quote_closed = false;
                    quote = *tmp;
                    while(tmp != _end)
                    {
                        ++tmp;
                        if( *tmp == quote )
                        {
                            is_quote_closed = true;
                            need_save_quoted_value = true;
                            ++tmp;						// переместить указатель за кавычки
                            break;
                        }
                    }
                }

                if(!is_quote_closed)						// не well_formed, нет закрывающих кавычек
                    break;

                // 2. любой разделитель - конец атрибута (разделители в кавычках обработались выше)
                // 3. атрибут вполне может граничить непосредственно с закрывающим символом '>'
                if ( isspace(*tmp) || *tmp == '>' || need_save_quoted_value)
                {
                    static char val[kMaxAttrValueLen];
                    size_t v_len = tmp - t;
                    bool is_bad_value = false;
                    int replace_len = 0;				// возможно придется менять scr на pure_pixel, поэтому резервируем длину

                    if (v_len > kMaxAttrValueLen)
                    {
                        printf("XS Error: too long attribute value:%i [max=%i]\n", v_len, kMaxAttrValueLen);
                        return -1;
                    }
                    if(v_len == 0) return -1;					// у атрибута нет значения. не well_formed.
                    else if(v_len == 1 && iscntrl(*t)) {
                        printf("XS Error: some cntrl symbol [int code: %i] possible XSS\n", (int)*t);
                        is_bad_value = true;
                    }

                    if(!need_save_quoted_value) strncpy(val, t, v_len);
                    else  	                                              // нужно вырезать значение атрибута из кавычек - "  some_value   "
                    {
                         if(v_len > 2)
                         {
                            const char* end_val;
                            do { ++t; } while(isspace(*t) && t != tmp );
                            end_val = tmp;
                            --end_val;
                            if(t == end_val) v_len = 0;
                            else {
                                do { --end_val; } while (isspace(*end_val));
                                v_len = end_val - t + 1;
                                strncpy(val, t, v_len);
                            }
                         }
                         else    			// if v_len = 2, value like attr=""
                             v_len = 0;
                    }
                    val[v_len] = '\0';

                    // Анализ атрибутов
                    if( strcmp(attr, "src")==0 || strcmp(attr, "lowsrc")==0 || strcmp(attr, "dynsrc")==0 )
                    {
                        m_Links.push_back(val);
                        // оставлять только что-то вроде src=http:bla , src=https:bla, src= "   http:bla" , src= "  https:bla"
                        static const char* good_attr_in_src[] = {"http", "https", "cid", "data"};
                        static size_t good_count = sizeof(good_attr_in_src) / sizeof (good_attr_in_src[0]);
                        char* pch;

                        if(has_forbidden_reference(val, _black_ref_list, _b_ref_list_size)) {
                            memset(val, 0, kMaxAttrValueLen);
                            is_black = true;
                        }

                        pch = strchr(val, ':');
                        if ( pch != NULL )
                        {
                            char* src_iterator = val;
                            size_t src_len = pch - src_iterator;
                            is_bad_value = true;		// если атрибут "хороший", значение изменится
                            if( src_len > 0)
                            {
                                size_t i;
                                for(i = 0; i < src_len; ++i)
                                    src_iterator[i] = tolower(src_iterator[i]);

                                for(i = 0; i < good_count; ++i)
                                {
                                    if (strncmp(src_iterator, good_attr_in_src[i], src_len ) == 0 )
                                    {
                                        is_bad_value = false;
                                        if(ban_img)
                                        {
                                            if(strcmp(tag->name,"img") == 0 && ( i == 0 || i == 1))			// check for banned images [only http and https]
                                            {
                                                size_t pos = strlen(_attr_str) + strlen(attr) + 1 + 1;	// attr="...
                                                char const *key;

                                                replace_len = strlen(pure_pixel) + 2 - v_len; // FIXME: Is it correct place for?

                                                key = strstr(val, "//");
                                                if(key)
                                                {
                                                    key += 2;
                                                    if (strncmp(key ,"www", 3) == 0)
                                                    {
                                                        key = strchr(key, '.');
                                                        if(key) ++key;
                                                    }
                                                }
                                                if(!key) break;

                                                xtext_list_insert(_xtext_list, key, pos, v_len);
                                            }
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }   // if (attr == src)
                    else if( strcmp(attr, target) == 0 )
                    {
                        has_target = true;
                    }
                    else if( strcmp(attr, href)   == 0 )
                    {
                        m_Links.push_back(val);
                        char* href_begin = val;
                        has_href = true;
                        if( *href_begin  == '#') 						                        href_link = IS_OKTOTORP;
                        else if (strncmp(href_begin, agent_link, strlen(agent_link)) == 0  )  	href_link = IS_AGENT;
                        else 									                                href_link = IS_OTHER;
                    }
                    else if( strcmp(attr, formtarget) == 0 && strcmp(tag->name, form) !=0 )
                    {
                        memset(val, 0, v_len);
                        strcpy(val, "_blank");
                        v_len = 6;
                    }
                    else if( strcmp(attr, "style") == 0 || strcmp(attr, "data-mce-style") == 0 )
                    {
                        int style_prop_count = 0;
                        char inner_style_quot = ' ';
                        static char checked_style[kMaxAttrValueLen];
                        memset(checked_style, 0,  kMaxAttrValueLen );

                        if(v_len > 2) style_prop_count = check_style(val, v_len, quote, &inner_style_quot, checked_style, _black_ref_list, _b_ref_list_size);

                        if(style_prop_count > 0)                                                                // если количество свойств в стиле больше 0, оставляем стиль
                        {
                            int ch_style_len = strlen(checked_style);
                            if(ch_style_len < kMaxAttrValueLen)
                            {
                                memcpy(val, checked_style, ch_style_len );
                                memset(val + ch_style_len, 0, kMaxAttrValueLen - ch_style_len);
                                v_len = ch_style_len;

                                if(quote == ' '){                                                               // обязательно заключить стиль в кавычки
                                    if(inner_style_quot == '"') quote = '\'';
                                    else quote = '"';
                                }
                            }
                            else
                                is_bad_value = true;
                        }
                        else                                                                                // в стиле не найдено ни одного свойства
                            is_bad_value = true;
                    }
                    else if( strcmp(attr, "action") == 0 || strcmp(attr, "formaction") == 0 )
                    {
                        has_action = true;
                        if(!is_url(val, v_len))
                        {
                            has_action = false;
                            is_bad_value = true;
                        }
                    }
                    else if ( strcmp(attr, "method") == 0 && strcmp(tag->name, "form") == 0)                // force 'method=POST' would be added later
                    {
                        is_bad_value = true;
                    }
                    else if ( strcmp(attr, "class") == 0)
                    {
                        if(strcmp(val, "mail-quote-collapse") == 0 || strcmp(val, "mailru-blockquote") == 0)
                            is_black = false;
                    }

                    if(!is_black && !is_bad_value )				// ===>  Now time for perl attribute check <=== //
                    {
                        static char from_perl_buf[kMaxAttrValueLen];
                        bool perl_checked = false;
                        int perl_checked_len = 0;
                        int cur_chunk_len = 0;
                        int attr_len = 0;

                        if( v_len > 2 )
                        {
                            const char* perl_method = 0;
                            char form_method[16] = {0};

                            if( has_action && (strcmp(attr, "action")==0 || strcmp(attr, "formaction") == 0) )
                            {
                                // need to extract method=POST or method=GET from this tag attributes
                                const char* attr_method = 0;
                                const char* tmp_cursor = _begin;
                                int form_i = 0;

                                perl_method = perl_make_hrefs_sub;

                                while( attr_method = strcasestr(tmp_cursor, "method") )
                                {
                                    attr_method += 6;
                                    while(*attr_method && isspace(*attr_method)) ++attr_method;
                                    if( *attr_method != '=') {tmp_cursor = attr_method; continue;}
                                    ++attr_method;
                                    while(*attr_method && !isalpha(*attr_method)) ++attr_method;
                                    while(*attr_method &&  isalpha(*attr_method) && form_i < 15) {form_method[form_i] = *attr_method; ++attr_method; ++form_i;}
                                    // XXX: finding of first pair like {method = post/get} or {method='post'/'get'}
                                    break;
                                    //tmp_cursor = attr_method;
                                }
                            }
                            else if(strcmp(attr, "href") == 0 && ( strcmp(tag->name,"a")==0 || strcmp(tag->name,"area")==0 ) )
                            {
                                if (is_url(val, v_len)) perl_method = perl_make_hrefs_sub;
                                else
                                {
                                    if(href_link == IS_OTHER)  // относительные ссылки ( ~ без протоколов) выпиливаются.
                                    {
                                        t = left = tmp;
                                        is_well_formed = true;
                                        break;
                                    }
                                }
                            }
                            else if( ( strcmp(attr, "src") == 0 && ( (strcmp(tag->name,"img")==0) || (strcmp(tag->name,"image")==0) || (strcmp(tag->name,"input")==0) ) ) ||
                                    strcmp(attr, "background") == 0 )
                                perl_method = perl_attr_check_sub;

                            if(perl_method)
                            {
                                int check;
                                memset(from_perl_buf, 0, kMaxAttrValueLen);
                                check = call_perl_attribute_checker(perl_method, tag->name , attr, val, from_perl_buf, quote, form_method);
                                if(check == -1)
                                {
                                    printf("XS Error: bad attribute -- %s [was ignored]\n", val);
                                    t = left = tmp;
                                    is_well_formed = true;
                                    break;
                                }
                                perl_checked = true;
                                perl_checked_len = strlen(from_perl_buf);
                            }
                        }

                        {
                            // Проверка на переполнение буфера
                            // Выплоняется с точностью до длины атрибута плюс длина значения атрибута
                            attr_len = strlen(attr);
                            cur_chunk_len = perl_checked? perl_checked_len : v_len;
                            cur_chunk_len += attr_len;

                            if( (total_len + cur_chunk_len) > kMaxFullClosedTagLen)
                            {
                                printf("XS Error: skipped too long attribute: %s (len %i) [max=%i]\n", attr, cur_chunk_len, kMaxFullClosedTagLen);
                                t = left = tmp;
                                is_well_formed = true;
                                break;
                            }
                        }

                        strncat(_attr_str, attr, attr_len);
                        strncat(_attr_str, "=", 1 );
                        total_len += (attr_len + 1);

                        if(perl_checked)
                        {
                            strncat(_attr_str, from_perl_buf, perl_checked_len);
                            total_len += perl_checked_len;
                        }
                        else
                        {
                            if(quote == ' ') quote = '\"';
                            strncat(_attr_str, &quote, 1);
                            strncat(_attr_str, val, v_len);
                            strncat(_attr_str, &quote, 1);
                            total_len += (v_len + 2);
                        }

                        if(replace_len > 0) while(replace_len--) strncat(_attr_str, " ", 1);

                        if( *tmp != '>' )
                            strncat(_attr_str, " ", 1);

                        ++count;
                    }

                    t = left = tmp;
                    is_well_formed = true;

                    break;
                }

            } // for

            if(!is_well_formed)					// неправильно оформленный атрибут
            {
                printf("XS Error: not well formed [tag was ignored -- %s %s=%s]\n", tag->name, attr, t);
                return -1;
            }

        } // if( *t == '=')

    }while( t != _end);

    if(!has_target && strcmp(tag->name, base) !=0 && ( (strcmp(tag->name,form) == 0)  || ( has_href && href_link == IS_OTHER ) ) )
    {
        strncat(_attr_str, " target=\"_blank\" ", 17);
        ++count;
    }
    if(href_link == IS_OKTOTORP)
    {
        strncat(_attr_str, " anchor=\"1\" ", 12);
        ++count;
    }
    if(strcmp(tag->name, "form") == 0)
    {
        if(has_action)
            strncat(_attr_str, " method=\"POST\" ", 15);
        else
            strncat(_attr_str, " action=\"#\" method=\"POST\" onsubmit=\"return false\" ", 50);       // <-- redirecting on /cgi-bin/#
            //strncat(_attr_str, " method=\"POST\" ", 15);                  // <-- redirecting on current page

        ++count;
    }

    /* UPD: Конец тега больше не пишем, так как всякие doctype в письме не юзаем, а остальные атрибуты должны соответствовать парадигме key=value
    // нужно дописать в конец атрибутов хвост тега.
    // если это, например, !doctype, то сюда попадает вся строка атрибутов такого тега
    for(; *left; ++left) if( !isspace(*left) ) break;
    {
        size_t left_size = strlen(left);
        if( left_size > 1)
        {
            strncat(_attr_str, left, left_size -1 );
        }
    }
    */
    return count;
}

static bool xss_clear_tag_at(mpop_string *str, size_t pos)
{
    char* cursor =  str->string + pos;
    if(*cursor != '<' ) return false;

    while( *cursor != '>')
    {
        *cursor = ' ';
        ++cursor;
    }
    *cursor = ' ';
    return true;
}

static inline bool xss_clear_tag(mpop_string* _str,  tag_t const *_tag)
/*
    clean some tag from result text.
*/
{
    return xss_clear_tag_at(_str, _tag->position);
}


static inline bool xss_replace_url(mpop_string* _str,  xtext_t* _text, const char* _new_text)
/*
    Замена текста в итоговом html. Пока меняются только URL'ы забаненных картинок на URL пустого пикселя.
    Длины на замену хватит, так как она специально резервировалась для этого в xss_attribute_parsing().
*/
{
    char* pos;
    char quote;
    bool quote_ok;

    if(_text->position == 0) return false;

    pos = &_str->string[_text->position -1];
    quote = *pos;

    if( !(quote == '\"' || quote == '\'' || quote == ' ') )					// just in case
        return false;

    ++pos;
    quote_ok = false;

    while( *_new_text )
    {
        *pos = *_new_text;
        ++_new_text;
        ++pos;
        if( *pos == quote) quote_ok = true;
    }
    *pos = quote;

    if( !quote_ok )
    {
        ++pos;
        while( *pos != quote )
        {
            char* tmp = pos;
            ++pos;
            *tmp = ' ';
        }
        *pos = ' ';
    }

    return true;
}

static inline int xs_encode_entities(char* str, int len, char* res)
{
    int i = 0;
    int j = -1;

    if(!str) return 0;

    for(i = 0; i < len; ++i) {

        if(j >= (kMaxAttrValueLen - 10)) {
            printf("XS Error: too long token\n");
            return ++j;
        }

        if(str[i] == '\"' )     {  res[++j] = '&';  res[++j] = 'q'; res[++j] = 'u'; res[++j] = 'o'; res[++j] = 't'; res[++j] = ';';  }
        else if (str[i] == '\'') { res[++j] = '&';  res[++j] = 'a'; res[++j] = 'p'; res[++j] = 'o'; res[++j] = 's'; res[++j] = ';';  }
        else if (str[i] == '<')  { res[++j] = '&';  res[++j] = 'l'; res[++j] = 't'; res[++j] = ';'; }
        else if (str[i] == '>')  { res[++j] = '&';  res[++j] = 'g'; res[++j] = 't'; res[++j] = ';'; }
        else if (str[i] == '&')  { res[++j] = '&';  res[++j] = 'a'; res[++j] = 'm'; res[++j] = 'p'; res[++j] = ';'; }
        else res[++j] = str[i];
    }

    return ++j;
}

#define BREAK_PARSE() \
{ \
    is_tag_open = false; \
    left = NULL; \
    break; \
}


const char **tag_white_list = NULL;
const char **attr_black_list = NULL;
const char **reference_black_list = NULL;
int t_count = 0;
int a_count = 0;

void add_tag(const char *tag)
{
    tag_white_list[t_count] = strdup(tag);
    ++t_count;
}
void add_attribute(const char *attr)
{
    attr_black_list[a_count] = strdup(attr);
    ++a_count;
}

int HtmlParser::init_params()
{
    if(tag_white_list == NULL)
    {
        tag_white_list = (const char**)malloc(40 * sizeof(char*));
        tag_white_list[39] = NULL;
        add_tag("a");
        add_tag("area");
        add_tag("b");
        add_tag("blockquote");
        add_tag("br");
        add_tag("center");
        add_tag("div");
        add_tag("em");
        add_tag("font");
        add_tag("form");
        add_tag("h1");
        add_tag("h2");
        add_tag("h3");
        add_tag("h4");
        add_tag("h5");
        add_tag("h6");
        add_tag("hr");
        add_tag("i");
        add_tag("img");
        add_tag("input");
        add_tag("map");
        add_tag("li");
        add_tag("ol");
        add_tag("p");
        add_tag("pre");
        add_tag("select");
        add_tag("small");
        add_tag("span");
        add_tag("strike");
        add_tag("strong");
        add_tag("sup");
        add_tag("table");
        add_tag("tbody");
        add_tag("td");
        add_tag("th");
        add_tag("tr");
        add_tag("tt");
        add_tag("u");
        add_tag("ul");
    }
    else
    {
        std::cerr << "Already inited...\n";
    }

    if(attr_black_list == NULL)
    {
        attr_black_list = (const char**)malloc(3 * sizeof(char*));
        attr_black_list[2] = NULL;
        add_attribute("class");
        add_attribute("id");
    }

    reference_black_list = NULL;
}

int HtmlParser::deinit_params()
{
    // TODO:
}

int HtmlParser::parse(const std::string &html)
{
    return parse(const_cast<char*>(html.data()), 1, 0, 0, 0, 0);
}

int HtmlParser::parse(char* _str, int _is_corp, int _img_jigurda, int _is_quote_collapse_user, int _user_id, long _msglist_flags)
{
    size_t size, memory_allocated, w_list_size = 0, b_list_size = 0, b_ref_list_size = 0;

    mpop_string valid_content;
    mpop_string plain_content;
    mpop_string title;

    const char** white_list  = NULL;
    const char** black_list  = NULL;
    const char** ref_black_list  = NULL;

    int banned_images_count = 0;				            // count of images in letter, who mark as banned in memcached
    int checked_images_count = 0;				            // count of all images in letter

    static const char* lt = "&lt;";
    static const char* gt = "&gt;";

    tag_nesting_t cur_nesting;
    tstack_t tag_stack;

    tag_t last_textarea = {"textarea", 0, 0};		    	// последний открытый тег <textarea>. если для него нет закрытого, то он заменяется на пробелы.

    char *right;
    char *left;
    char *last_closed_tag;		                            // за последней закрытой '>'
    char *last_open_tag;		                            // последняя открытая '<'

    bool is_tag_open = false;

    xtext_list_t xtext_list = {0, 0, 0, 0};	                // подозрительный текст (урлы), который возможно придется заменить в конце обработки

    if(!_str) return 0;

    size = strlen(_str);
    if(!size) return 0;
    memory_allocated = sizeof(char) * (size + 1);

    init_string(&valid_content);
    allocate_string(&valid_content, memory_allocated);

    init_string(&plain_content);
    allocate_string(&plain_content, 1024);

    init_string(&title);
    add_string(&title, "");

    if(tag_white_list != 0)
    {
        int counter = 0;
        while(tag_white_list[counter]) ++counter;
        if(counter !=0)
        {
            white_list = tag_white_list;
            w_list_size = counter;
        }
    }

    if(attr_black_list != 0)
    {
        int counter = 0;
        while(attr_black_list[counter]) ++counter;
        if(counter != 0)
        {
            black_list = attr_black_list;
            b_list_size = counter;
        }
    }

    if(reference_black_list != 0)
    {
        int counter = 0;
        while(reference_black_list[counter]) ++counter;
        if(counter != 0)
        {
            ref_black_list = reference_black_list;
            b_ref_list_size = counter;
        }
    }

    memset(&cur_nesting, 0, sizeof(cur_nesting));
    stack_init(&tag_stack);

    is_danger_content	= false;				// setting in true, if expression, script, behavior e.t.c. found	[global]
    is_memc_timeouted	= false;				// it setting in true when SIGALARM catch			            	[global]
    is_corp_user	    = _is_corp;				// if true -> is corp user					                    	[global]

    right	        = _str;
    left            = _str;
    last_closed_tag = _str;		                    // за последней закрытой '>'
    last_open_tag	= _str;		                    // последняя открытая '<'

    for(; *right; ++right)
    {
        // see MAIL-18656
        // Чтобы починить text/html письма с символами U2028/U2029, надо это раскомментить.
        // Однако почему-то симовлы u2028/u2029 влияют на верстку только если они есть в text_plain!!!
        // Пока этот вопрос остается открытым, комментарий не стирать
        // check_forbidden_utf(right, left);

        if( *right == '<' && !is_tag_open)
        {
            char *cut_it;

            // save text [last_closed_tag, right)/
            if(last_closed_tag && (right - last_closed_tag) > 0)
            {
                html_text_process (last_closed_tag, right - last_closed_tag, &cur_nesting, &valid_content);
                plain_text_process(last_closed_tag, right - last_closed_tag, &cur_nesting, &plain_content);
            }

            last_open_tag = left = right;

            // проверка на комментарий. выполняется для каждой скобки '<', так как внутри комментариев могут быть скобки.
            cut_it = NULL;
            if(xss_cut_comments(right, (char const **)&cut_it))
            {
                if(cut_it)						// вырезаем все до cut_it
                {
                    last_open_tag = last_closed_tag = left = cut_it;
                    right = cut_it - 1;
                    continue;
                }
                else							// незакрытый комментарий, обработка завершается.
                {
                    left = NULL;
                    break;
                }
            }

            is_tag_open = true;
            continue;
        }

        if( *right == '<' && is_tag_open)
        {
            // save text текст [last_open_tag, right) replace last '<' on &lt;
            if(last_open_tag && (right - last_open_tag) > 0)
            {
                add_string(&valid_content, lt);
                add_string(&plain_content, "<");

                ++left;						// передвинули left с символа '<'

                html_text_process (left, right - left, &cur_nesting, &valid_content);
                plain_text_process(left, right - left, &cur_nesting, &plain_content);
            }

            last_open_tag = left = right;
            continue;
        }

        if( *right == '>' && !is_tag_open)				// считаем, что это &gt;
        {
            // [last_closed_tag, right)
            if( last_closed_tag && (right - last_closed_tag) > 0 )
            {
                html_text_process(last_closed_tag, right - last_closed_tag, &cur_nesting, &valid_content);
                add_string(&valid_content, gt);

                plain_text_process(last_closed_tag, right - last_closed_tag, &cur_nesting, &plain_content);
                add_string(&plain_content, ">");
            }

            if( right == last_closed_tag)				// идущие подряд '>'
            {
                add_string(&valid_content, gt);
                add_string(&plain_content, ">");
            }

            left = right;
            last_closed_tag = right + 1;
            continue;
        }

        if(*right == '>' && is_tag_open)				// something in <...>
        {
            tag_t cur_tag;		                    				// текущий тег
            bool is_cur_tag_textarea = false;
            int is_a_tag;
            char *cut_it;
            char save_me;
            int tag_len;

            last_closed_tag = right + 1;

            is_a_tag = xss_is_tag(last_open_tag, last_closed_tag);
            if( is_a_tag == -1)							// its not tag
            {
                add_string(&valid_content, lt);
                add_string(&plain_content, "<");

                ++left;

                html_text_process(left, right - left, &cur_nesting, &valid_content);
                add_string(&valid_content, gt);

                plain_text_process(left, right - left, &cur_nesting, &plain_content);
                add_string(&plain_content, ">");

                is_tag_open = false;
                left = right;
                continue;
            }

            // некоторые теги необходимо вырезать с кишками.
            cut_it = NULL;
            if(xss_get_tag(last_open_tag, &cur_tag) && xss_cut_some_tags(last_open_tag, (char const **)&cut_it, &title, &cur_tag )  )  	// all tags = tolower (tags) here!
            {
                if(cut_it)								// вырезаем все до cut_it
                {
                    last_open_tag = last_closed_tag = left = cut_it;
                    right = cut_it - 1;
                    is_tag_open = false;
                    continue;
                }
                else									// незакрытый тег или комментарий в теге style, обработка завершается.
                {
                    is_tag_open = false;
                    left = NULL;
                    break;
                }
            }

            if( strcmp(cur_tag.name, "a") == 0 )
            {
                if (cur_tag.open) ++cur_nesting.in_href;
                else --cur_nesting.in_href;
            }
            else if( strcmp(cur_tag.name, "td") == 0 || strcmp(cur_tag.name, "th") == 0)
            {
                stack_push_id(&tag_stack, E_TD, (cur_tag.open)? true : false, &cur_nesting, &valid_content);
                if(tag_stack.overflow)
                {
                    BREAK_PARSE();
                }
            }
            else if( strcmp(cur_tag.name, "table") == 0 )
            {
                bool push = stack_push_id(&tag_stack, E_TABLE, (cur_tag.open)? true : false, &cur_nesting, &valid_content);
                if(tag_stack.overflow)
                {
                    BREAK_PARSE();
                }
                if(!push)
                {
                    is_tag_open = false;
                    left = right;
                    cur_nesting.in_table = 0;
                    continue;
                }
            }
            else if( strcmp(cur_tag.name, "pre") == 0 )
            {
                if (cur_tag.open) ++cur_nesting.in_pre;
                else --cur_nesting.in_pre;
            }
            else if( strcmp(cur_tag.name, "blockquote") == 0 )
            {
                if (cur_tag.open) ++cur_nesting.in_blockquote;
                else --cur_nesting.in_blockquote;
            }
            else if( strcmp(cur_tag.name, "div") == 0 )
            {
                bool push = stack_push_id(&tag_stack, E_DIV, (cur_tag.open)? true : false, &cur_nesting, &valid_content);
                if(tag_stack.overflow)
                {
                    BREAK_PARSE();
                }
                if(!push)
                {
                    is_tag_open = false;
                    left = right;
                    cur_nesting.in_div = 0;
                    continue;
                }
            }
            else if( strcmp(cur_tag.name, "span") == 0 )
            {
                if (cur_tag.open) ++cur_nesting.in_span;
                else --cur_nesting.in_span;
            }
            else if( strcmp(cur_tag.name, "textarea") == 0 )
            {
                is_cur_tag_textarea = true;
                if(last_textarea.open )
                {
                    if(cur_tag.open)
                    {
                        xss_clear_tag(&valid_content, &last_textarea);
                    }
                    else
                        last_textarea.open = false;
                }
                else
                {
                    if(cur_tag.open)
                        last_textarea.open = true;
                    else
                    {
                        // its bad..  here closed </textarea>, but no open <textarea>.. what to do in this case???
                    }
                }
            }

            if(cur_tag.open)
            {
                char* pc = cur_tag.name;
                char** found_it = (char**)bsearch(&pc, block_tags, block_tags_count, sizeof(char*), mb_compare_string_p);

                if( (strcmp(cur_tag.name,"br") == 0 ) || (found_it && (plain_content.size == 0 || plain_content.string[plain_content.size - 1] != '\n' ) ) )
                {
                    if( plain_content.size > 0 && plain_content.string[plain_content.size - 1] == '\n' ) add_blockquote_to_plain(cur_nesting.in_blockquote, &plain_content);
                    add_char(&plain_content, '\n');
                }

                if( strcmp(cur_tag.name,"li") == 0 ) 		{ add_blockquote_to_plain(cur_nesting.in_blockquote, &plain_content); add_stringn(&plain_content, "* ", 2);     }
                else if( strcmp(cur_tag.name,"dt") == 0 )	{ add_blockquote_to_plain(cur_nesting.in_blockquote, &plain_content); add_stringn(&plain_content, "+ ", 2);     }
                else if( strcmp(cur_tag.name,"dd") == 0 )	{ add_blockquote_to_plain(cur_nesting.in_blockquote, &plain_content); add_stringn(&plain_content, "- ", 2);     }
                else if( strcmp(cur_tag.name,"hr") == 0 )	{ add_blockquote_to_plain(cur_nesting.in_blockquote, &plain_content); add_stringn(&plain_content, plain_hr, 71);}
            }

            save_me = *last_closed_tag;
            *last_closed_tag = 0;							// now left is С-string

            tag_len = last_closed_tag - last_open_tag;

            if(tag_len < kMaxFullClosedTagLen)
            {
                if(xss_is_white_tag(&cur_tag, white_list, w_list_size))
                {
                    char *space_position = left;
                    for(; *space_position; ++space_position)
                        if( (*space_position != ' ') && (*space_position != '\t') && (*space_position != '<' ) ) break;	// переместили space_position внутрь скобок <  >
                    while(space_position != right)						// поиск разделителя между тегом и атрибутами
                    {
                        ++space_position;
                        if (isspace(*space_position)) break;					// ' ', '\r', '\t', '\n' etc.
                    }

                    if(cur_tag.open)
                    {
                        size_t attr_size = last_closed_tag - space_position;
                        char attributes[kMaxFullClosedTagLen] = {'\0'};
                        int attr_count = 0;
                        int xtext_list_count_before = xtext_list.count;

                        if(space_position != right)
                        {
                            attr_count = xss_attribute_parsing(space_position, right, &cur_tag, black_list, b_list_size, ref_black_list, b_ref_list_size, &xtext_list, attributes);
                        }
                        else
                        {
                            --space_position;
                            if( (strcmp(cur_tag.name,"form")) == 0 )
                            strcpy(attributes, " target=\"_blank\" action=\"#\" method=\"POST\" onsubmit=\"return false\"");
                        }

                        if(is_cur_tag_textarea) last_textarea.position = valid_content.size;
                        if(*space_position == '\r') *space_position = '\n';						                            // возврат каретки

                        add_stringn(&valid_content, last_open_tag, space_position + 1 - last_open_tag);		                // '<' + тег + space

                        if(attr_count >= 0)
                        {
                            if ( xtext_list.count > xtext_list_count_before ) xtext_list.last->position += valid_content.size;
                            add_string(&valid_content, attributes);					                    // атрибуты
                        }
                        else if ( attr_count == -1 )
                        {
                            if ( xtext_list.count > xtext_list_count_before ) xtext_list.last->position = 0;
                            // need only tag
                        }

                        add_stringn(&valid_content, right, 1);							                                    // + '>'
                    }
                    else
                    {
                        if(space_position == right) // </div>
                            add_stringn(&valid_content, last_open_tag, last_closed_tag - last_open_tag);
                        else  { // </div 123>
                            add_stringn(&valid_content, last_open_tag, space_position + 1 - last_open_tag);     // '<' + тег + space
                            add_stringn(&valid_content, right, 1);                                              // + '>'
                        }
                    }
                }
            }
            else
                printf("XS Error: too long tag:%i [max=%i]\n", tag_len, kMaxFullClosedTagLen);

            is_tag_open = false;
            *last_closed_tag = save_me;
            left = right;
        }	// if(*right == '>' && is_opened_tag)

        if(*right == '\0') break;     // just in case

    } // for(; *right; ++right)

    // Дописывание конца. left стоит на последней скобке '<' или '>', либо на начале текста (если не было ни одних скобок). left = NULL, если не найден конец тега или комментария.
    if(left != NULL)
    {
        if(is_tag_open)
        {
            add_string(&valid_content, lt);
            add_string(&plain_content, "<");
            ++left;
        }
        else
            if(left != _str)		// если в _str нет ни одной скобочки, то left оставить на месте.
                ++left;

        // больше уголков '>' и '<' в тексте нет, но еще могут быть entities!
        if( (right - left) > 0 )
        {
            add_stringn(&valid_content, left, right - left);
            plain_text_process(left, right - left, &cur_nesting,  &plain_content);
        }
    }

    if(last_textarea.open)
        xss_clear_tag(&valid_content, &last_textarea);

    //stack_print("END.", &tag_stack);
    while(cur_nesting.in_table--)
    {
        tag_nesting_t n;
        const stack_tag_t *t = found_tag_in_current_table(&tag_stack, E_TD, true, &n);
        if(t && t->manual)
        {
            stack_push_manual(&tag_stack, E_TD, false);
            add_stringn(&valid_content, "</td>", 5);
        }
        add_stringn(&valid_content, "</table>", 8);
        stack_pop_table(&tag_stack, &n);
    }

    m_HtmlText.assign(valid_content.string, valid_content.size);
    m_PlainText.assign(plain_content.string, plain_content.size);

    free_string(&valid_content);
    free_string(&plain_content);
    free_string(&title);
    return 1;
}


