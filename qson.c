#include "qson.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define QSON_EXPECT(cond, msg)      \
if (!(cond)) {                        \
    qson_error(doc, msg);                \
    return NULL;                    \
}

static void qson_error(qson_doc_t* doc, const char* format, ...);
static char next(qson_doc_t* doc);
static int try(qson_doc_t* doc, char x);
static qson_value_t* qson_new(qson_doc_t* doc, qson_type_t t);
static void qson_object_add(qson_value_t* obj, qson_value_t* pair);
static void qson_pool_init(qson_pool_t* pool, int chunk_size);
static qson_value_t* qson_alloc(qson_doc_t* doc);
static void qson_parse_comment(qson_doc_t* doc);
static qson_value_t* qson_parse_object(qson_doc_t* doc);
static qson_value_t* qson_parse_pair(qson_doc_t* doc);
static qson_value_t* qson_parse_value(qson_doc_t* doc);
static qson_value_t* qson_parse_null(qson_doc_t* doc);
static qson_value_t* qson_parse_bool(qson_doc_t* doc);
static qson_value_t* qson_parse_number(qson_doc_t* doc);
static qson_value_t* qson_parse_array(qson_doc_t* doc);
static qson_value_t** qson_array_push(qson_value_t* arr);
static qson_value_t* qson_array_back(qson_value_t* arr);
static qson_value_t* qson_add_array(qson_doc_t* doc, qson_value_t* arr);
static qson_value_t* qson_parse_token(qson_doc_t* doc);


static void qson_error(qson_doc_t* doc, const char* format, ...)
{
    fprintf(stderr, "error: line %d: ", doc->line);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

static char next(qson_doc_t* doc)
{
    char* p = doc->p;
    while (1) {
        while (*p == ' ' || *p == '\t' 
                || *p == '\r' || *p == '\n') {
            if (*p == '\n')
                doc->line++;
            p++;
        }
            
        if (*p == '#') {
            qson_parse_comment(doc);
        } else {
            break;
        }
    }
    
    doc->p = p;
    if (*p != '\0') {
        doc->p++;
    }
    
    return *p;
}

static int try(qson_doc_t* doc, char x)
{
    // Check empty array
    char* p = doc->p;
    char ch = next(doc);
    if (ch == x)
        return 1;
    doc->p = p;
    return 0;
}

static qson_value_t* qson_new(qson_doc_t* doc, qson_type_t t)
{
    qson_value_t* val = qson_alloc(doc);
    if (val == NULL)
        return val;
        
    memset(val, 0, sizeof(qson_value_t));
    val->t = t;
    return val;
}

static void qson_object_add(qson_value_t* obj, qson_value_t* pair)
{
    assert(pair->t == QSON_PAIR);
    if (obj->head == NULL) {
        obj->head = pair;
        obj->tail = pair;
    } else {
        obj->tail->next = pair;
        obj->tail = obj->tail->next;
    }
}

int qson_load(qson_doc_t* doc, char* file_name)
{
    doc->file = fopen(file_name, "rb");
    if (doc->file == NULL) {
        qson_error(doc, "open file: '%s'' failed", file_name);
        return QSON_ERR;
    }
    
    size_t len;
    fseek(doc->file, 0, SEEK_END);
    len = ftell(doc->file);
    fseek(doc->file, 0, SEEK_SET);
    
    doc->mem = malloc(len + 1);
    if (doc->mem == NULL) {
        qson_error(doc, "no memory");
        fclose(doc->file);
        return QSON_ERR;
    }
    
    fread(doc->mem, 1, len, doc->file);
    doc->mem[len] = '\0';
    
    return QSON_OK;
}

static void qson_pool_init(qson_pool_t* pool, int chunk_size)
{
    pool->chunk_size = chunk_size;
    pool->allocated_n = 0;
    pool->cur = NULL;
    
    pool->chunk_arr.t = QSON_ARRAY;
    pool->chunk_arr.size = 0;
    pool->chunk_arr.capacity = 0;
    pool->chunk_arr.adata = NULL;
}

static qson_value_t* qson_alloc(qson_doc_t* doc)
{
    qson_pool_t* pool = &doc->pool;
    qson_value_t* back = qson_array_back(&pool->chunk_arr);
    if (pool->cur == NULL || pool->cur - back == pool->chunk_size) {
        qson_value_t** chunk = qson_array_push(&pool->chunk_arr);
        if (chunk == NULL)
            return NULL;
        *chunk = malloc(pool->chunk_size * sizeof(qson_value_t));
        QSON_EXPECT(*chunk != NULL, "no memory");
        pool->cur = qson_array_back(&pool->chunk_arr);
    }
    pool->allocated_n++;
    return pool->cur++;
}

void qson_destroy(qson_doc_t* doc)
{
    // Destroy pool
    qson_pool_t* pool = &doc->pool;
    for (int i = 0; i < pool->chunk_arr.size; i++)
        free(pool->chunk_arr.adata[i]);
    free(pool->chunk_arr.adata);
    pool->chunk_arr.adata = NULL;
    
    // Release file and memory
    fclose(doc->file);
    free(doc->mem);
    
    doc->obj = NULL;
    
    qson_value_t* p = doc->arr_list.next;
    while (p != NULL) {
        assert(p->data->t == QSON_ARRAY);
        // The elements are allocated in pool.
        // Thus they shouldn't be freed.
        free(p->data->adata);
        p = p->next;
    }
}

qson_value_t* qson_parse(qson_doc_t* doc)
{
    doc->obj = NULL;
    doc->p = doc->mem;
    doc->line = 1;
    doc->ele_num = 0;
    
    doc->arr_list.t = QSON_LIST;
    doc->arr_list.data = NULL;
    doc->arr_list.next = NULL;
    
    qson_pool_init(&doc->pool, 16);
    
    if (next(doc) == '{') {
        doc->obj = qson_parse_object(doc);
    }
    
    return doc->obj;
}

qson_value_t* qson_parse_string(qson_doc_t* doc, char* str)
{
    doc->mem = str;
    return qson_parse(doc);
}

/*
 * Json defines no comment, the comment qson specified,
 * is the Python style comment(leading by '#')
 */
static void qson_parse_comment(qson_doc_t* doc)
{
    while (*doc->p != '\n' && *doc->p != 0)
        doc->p++;
    if (*doc->p == '\n') {
        doc->line++;
        doc->p++;
    }
}

static qson_value_t* qson_parse_object(qson_doc_t* doc)
{
    qson_value_t* obj = qson_new(doc, QSON_OBJECT);
    if (obj == NULL)
        return NULL;
    
    if (try(doc, '}'))
        return obj;
        
    while (1) {
        QSON_EXPECT(next(doc) == '\"', "unexpected character");
        qson_value_t* pair = qson_parse_pair(doc);
        if (pair == NULL)
            return NULL;
        qson_object_add(obj, pair);
        
        char ch = next(doc);
        if (ch == '}')
            break;
        QSON_EXPECT(ch == ',', "expect ','");
    }
    return obj;
}

static qson_value_t* qson_parse_pair(qson_doc_t* doc)
{
    qson_value_t* pair = qson_new(doc, QSON_PAIR);
    if (pair == NULL)
        return NULL;

    pair->key = qson_parse_token(doc);
    if (pair->key == NULL)
        return NULL;
    
    QSON_EXPECT(next(doc) == ':', "expect ':'");
    
    pair->val = qson_parse_value(doc);
    if (pair->val == NULL)
        return NULL;

    return pair;
}

static qson_value_t* qson_parse_value(qson_doc_t* doc)
{
    switch (next(doc)) {
    case '{':
        return qson_parse_object(doc);
    case '[':
        return qson_parse_array(doc);
    case '\"':
        return qson_parse_token(doc);
    case '0' ... '9':
    case '-':
        return qson_parse_number(doc);
    case 't':
    case 'f':
        return qson_parse_bool(doc);
    case 'n':
        return qson_parse_null(doc);
    default:
        QSON_EXPECT(0, "unexpect character");
    }
        
    return NULL; // Make compiler happy
}

static qson_value_t* qson_parse_null(qson_doc_t* doc)
{
    char* p = doc->p;
    if (p[0] == 'u' && p[1] == 'l' && p[2] == 'l') {
        doc->p = p + 3;
        return qson_new(doc, QSON_NULL);
    }
    return NULL;
}

static qson_value_t* qson_parse_bool(qson_doc_t* doc)
{
    qson_value_t* b = qson_new(doc, QSON_BOOL);
    if (b == NULL)
        return NULL;
    
    char* p = doc->p - 1;
    if (p[0] == 't' && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
        b->bval = 1;
        doc->p = p + 4;
    } else if (p[0] == 'f' && p[1] == 'a'
            && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
        b->bval = 0;
        doc->p = p + 5;
    } else {
        QSON_EXPECT(0, "unexpect 'true' or 'false'");
    }
    
    return b;
}

static qson_value_t* qson_parse_number(qson_doc_t* doc)
{
    char* begin = doc->p - 1;
    char* p = begin; // roll back
    if (p[0] == '-')
        p++;
    if (p[0] == '0' && isdigit(p[1])) {
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
    
ret:;
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

static qson_value_t* qson_parse_array(qson_doc_t* doc)
{
    qson_value_t* arr = qson_new(doc, QSON_ARRAY);
    if (arr == NULL)
        return NULL;
    if (qson_add_array(doc, arr) == NULL)
        return NULL;
        
    if (try(doc, ']'))
        return arr;
        
    while (1) {
        qson_value_t** ele = qson_array_push(arr);
        if (ele == NULL)
            return NULL;
        
        *ele = qson_parse_value(doc);
        if (*ele == NULL)
            return NULL;
        
        char ch = next(doc);
        if (ch == ']')
            break;
        QSON_EXPECT(ch == ',', "expect ',' or ']'");
    }
    return arr;
}

static qson_value_t** qson_array_push(qson_value_t* arr)
{
    if (arr->size >= arr->capacity) {
        int new_c = arr->capacity * 2 + 1;
        arr->adata = (qson_value_t**)realloc(arr->adata,
                new_c * sizeof(qson_value_t*));
        if (arr->adata == NULL) {
            fprintf(stderr, "error: no memory");
            return NULL;
        }
        
        arr->capacity = new_c;
    }
    
    return &arr->adata[arr->size++];
}

static qson_value_t* qson_array_back(qson_value_t* arr)
{
    if (arr->adata == NULL)
        return NULL;
    return arr->adata[arr->size - 1];
}

static qson_value_t* qson_add_array(qson_doc_t* doc, qson_value_t* arr)
{
    qson_value_t* node = qson_new(doc, QSON_LIST);
    if (node == NULL)
        return NULL;
    
    node->data = arr;
    node->next = doc->arr_list.next;
    doc->arr_list.next = node;
    
    return node;
}


static qson_value_t* qson_parse_token(qson_doc_t* doc)
{
    qson_value_t* str = qson_new(doc, QSON_STRING);
    if (str == NULL)
        return NULL;
    
    char* p = doc->p;
    str->sdata = p;
    for (; ; p++) {
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
                    QSON_EXPECT(isxdigit(*p++), "expect hexical");
                break;
            default:
                QSON_EXPECT(0, "unexpected control label");
            }
            break;
        
        case '\"':
            *p = '\0';  // to simplify printf
            doc->p = p + 1;
            str->len = p - str->sdata;
            return str;
        
        case '\0':
            QSON_EXPECT(0, "unexpected end of file, expect '\"'");    
        default:
            break;
        }
    }
    return str; // Make compiler happy
}
