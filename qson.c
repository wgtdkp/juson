#include "qson.h"
#include <stdio.h>
#include <stdlib.h>


#define QSON_EXPECT(cond, msg)      \
if (!cond) {                        \
    qson_error(msg);                \
    return NULL;                    \
}

void qson_error(const char* format, ...)
{
    
}

int next(doc)
{
    char* p = doc->p;
    while (1) {
        while (*p == ' ' || *p == '\t' || *p == '\r')
            p++;
        if (*p != '\n')
            break;
        p++;
    }
    doc->p = p + 1;
    return *p;
}

qson_value_t* qson_new(qson_doc_t* doc, qson_type_t t)
{
    qson_value_t* val = malloc(sizeof(qson_value_t));
    if (val == NULL) {
        qson_err("no memory");
        return NULL;
    }
    memset(val, 0, sizeof(qson_value_t));
    val->t = t;
    return val;
}

void qson_object_add(qson_value_t* obj, qson_value_t* pair)
{
    assert(pair->t == QSON_PAIR);
    if (obj->head == NULL) {
        obj->head = pair;
        obj->tail = head;
    } else {
        obj->tail->next = pair;
        obj->tail = obj->tail->next;
    }
}

int qson_parse(qson_doc_t* doc)
{
    while (1) {
        int err;
        switch (next(doc)) {
        case '#':
            qson_parse_comment(doc);
            break;
        case '{':
            doc->obj = qson_parse_object(doc);
            if (doc->obj == NULL)   
                return QSON_ERR;
            break;
        default:
            return QSON_ERR;
        }
        if (err == QSON_ERR)
            return QSON_ERR;
    }
    
    return QSON_OK;
}

/*
 * Json defines no comment, the comment qson specified,
 * is the Python style comment(leading by '#')
 */
void qson_parse_comment(qson_doc_t* doc)
{
    // Always append '\n' at the end of file
    while (*doc->p != '\n')
        doc->p++;
    doc->p++;
}

qson_value_t* qson_parse_object(qson_doc_t* doc)
{
    qson_value_t* obj = qson_new(doc, QSON_OBJECT);
    if (obj == NULL)
        return NULL;
    
    while (1) {
        int ch = next(doc);
        QSON_EXPECT(ch == '\"', "unexpected character");
        
        qson_value_t* pair = qson_parse_pair(doc, obj);
        if (pair == NULL)
            return NULL;
        qson_object_add(obj, pair);
        
        ch = next(doc);
        if (ch == '}')
            break;
        QSON_EXPECT(ch == ',', "expect ','");
    }
    return QSON_OK;
}

qson_value_t* qson_parse_pair(qson_doc_t* doc)
{
    qson_value_t* pair = qson_new(doc, QSON_PAIR);
    if (pair == NULL)
        return NULL;
        
    qson_object_add(obj, pair);
    
    pair->key = qson_parse_string(doc);
    if (pair->key == NULL)
        return NULL;
    
    QSON_EXPECT(next(doc) == ':', "expect ':'");
    
    pair->val = qson_parse_value(doc);
    if (pair->val == NULL)
        return NULL;
        
    return pair;
}

qson_value_t* qson_parse_value(qson_doc_t* doc)
{
    switch (next(doc)) {
    case '{':
        return qson_parse_object(doc);
    case '[':
        return qson_parse_array(doc);
    case '\"':
        return qson_parse_string(doc);
    case '0' ... '9':
    case '-':
        return qson_parse_number(doc);
    case 't':
    case 'f':
        return qson_parse_bool(doc);
    default:
        QSON_EXPECT(0, "unexpect character");
    }
        
    return NULL; // Make compiler happy
}

qson_value_t* qson_parse_bool(qson_doc_t* doc)
{
    qson_value_t* b = qson_new(doc, QSON_BOOL);
    if (b == NULL)
        return NULL;
    
    char* p = doc->p - 1;
    if (p[0] == 't' && p[1] == 'r' && [2] == 'u' && p[3] == 'e')
        b->bval = 1;
    else if (p[0] == 'f' && p[1] == 'a'
            && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
        b->bval = 0;
    } else {
        QSON_EXPECT(0, "unexpect 'true' or 'false'");
    }
    return val;
}

qson_value_t* qson_parse_number(qson_doc_t* doc)
{
    char* begin = doc->p - 1;
    char* p = begin; // roll back
    if (p[0] == '-')
        p++;
    if (p[0] == '0' && is_digit(p[1])) {
        QSON_EXPECT(0, "number leading by '0'");
    }
    
    int saw_dot = 0;
    int saw_e = 0;
    while (1) {
        switch (*p) {
        case '0' ... '9':
            break;
        
        case '.':
            QSON_EXPECT(!saw_e, "exponential term must be integer");
            QSON_EXPECT(!saw_dot, "unexpected '.'");
            saw_dot = 1;
            break;
        
        case 'e':
        case 'E':
            if (p[1] == '-' || p[1] == '+')
                p++;
            QSON_EXPECT(!saw_e, "unexpected 'e'('E')");
            saw_e = 1;
            break;
        default:
            goto ret;
        }
        p++;
    }
    
ret:
    qson_value_t* val;
    if (saw_dot || saw_e) {
        val = qson_new(doc, QSON_FLOAT);
        val->fval = atof(begin);
    } else {
        val = qson_new(doc, QSON_INT);
        val->ival = atoi(begin);  
    }
    
    doc->p = p;
    return val;
}

qson_value_t* qson_parse_array(qson_doc_t* doc)
{
    qson_value_t* arr = qson_new(doc, QSON_ARRAY);
    if (arr == NULL)
        return NULL;
    if (qson_add_array(doc, arr) == NULL)
        return NULL;
    
    while (1) {
        qson_value_t** ele = qson_array_push(doc, arr);
        if (ele == NULL)
            return NULL;
        
        *ele = qson_parse_value(doc);
        if (*ele == NULL)
            return NULL;
        
        char ch = next(ch);
        if (ch == ']')
            break;
        QSON_EXPECT(ch == ',', "expect ',' or ']'");
    }
    return arr;
}

qson_value_t** qson_array_push(qson_doc_t* doc, qson_value_t* arr)
{
    if (arr->size >= arr->capacity) {
        int new_c = arr->capacity * 2 + 1;
        arr->data = (qson_value_t**)realloc(arr->data,
                new_c * sizeof(qson_value_t*));
        
        QSON_EXPECT(arr->data != NULL, "no memory");
            
        arr->capacity = new_c;
    }
    
    return arr->adata[arr->size++];
}

qson_value_t* qson_add_array(qson_doc_t* doc, qson_value_t* arr)
{
    qson_value_t* node = qson_new(doc, QSON_LIST);
    if (node == NULL)
        return NULL;
    
    node->data = arr;
    node->next = doc->arr_list.next;
    doc->arr_list.next = node;
    
    return node;
}


qson_value_t* qson_parse_string(qson_doc_t* doc)
{
    qson_value_t* str = qson_new(doc, QSON_STRING);
    if (str == NULL)
        return NULL;
    
    char* p = doc->p;
    str->data = p;
    while (1) {
        switch (*p) {
        case '\\':
            switch (*++p) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u':
                for (int i = 0; i < 4; i++)
                    QSON_EXPECT(is_hex(*p++), "expect hexical");
                break;
            default:
                QSON_EXPECT(0, "unexpected control label");
            }
            break;
        
        case '\"':
            doc->p = p + 1;
            str->len = p - str->data;
            return str;
        
        case '\0':
            QSON_EXPECT(0, "unexpected end of file, expect '\"'");    
        default:
            break;
        }
    }
    return str; // Make compiler happy
}
