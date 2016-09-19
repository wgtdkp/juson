#include "juson.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JUSON_EXPECT(cond, msg)     \
if (!(cond)) {                      \
    juson_error(doc, msg);          \
    return NULL;                    \
};

static void juson_error(juson_doc_t* doc, const char* format, ...);
static char next(juson_doc_t* doc);
static int try(juson_doc_t* doc, char x);
static juson_value_t* juson_new(juson_doc_t* doc, juson_type_t t);
static void juson_object_add(juson_value_t* obj, juson_value_t* pair);
static void juson_pool_init(juson_pool_t* pool, int chunk_size);
static juson_value_t* juson_alloc(juson_doc_t* doc);
static char* juson_parse_comment(juson_doc_t* doc, char* p);
static juson_value_t* juson_parse_object(juson_doc_t* doc);
static juson_value_t* juson_parse_pair(juson_doc_t* doc);
static juson_value_t* juson_parse_value(juson_doc_t* doc);
static juson_value_t* juson_parse_null(juson_doc_t* doc);
static juson_value_t* juson_parse_bool(juson_doc_t* doc);
static juson_value_t* juson_parse_number(juson_doc_t* doc);
static juson_value_t* juson_parse_array(juson_doc_t* doc);
static juson_value_t** juson_array_push(juson_value_t* arr);
static juson_value_t* juson_add_array(juson_doc_t* doc, juson_value_t* arr);
static juson_value_t* juson_parse_token(juson_doc_t* doc);

static void juson_error(juson_doc_t* doc, const char* format, ...)
{
    fprintf(stderr, "error: line %d: ", doc->line);    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static char next(juson_doc_t* doc)
{
    char* p = doc->p;
    while (1) {
        while (*p == ' ' || *p == '\t' 
                || *p == '\r' || *p == '\n') {
            if (*p == '\n')
                doc->line++;
            p++;
        }
            
        if (*p == '/')
            p = juson_parse_comment(doc, p + 1);
        else
            break;
    }
    doc->p = p;
    if (*p != '\0')
        ++doc->p;
    return *p;
}

static int try(juson_doc_t* doc, char x)
{
    // Check empty array
    char* p = doc->p;
    int line = doc->line;
    char ch = next(doc);
    if (ch == x)
        return 1;    
    doc->p = p;
    doc->line = line;
    return 0;
}

static juson_value_t* juson_new(juson_doc_t* doc, juson_type_t t)
{
    juson_value_t* val = juson_alloc(doc);
    if (val == NULL)
        return val;
    memset(val, 0, sizeof(juson_value_t));
    val->t = t;
    return val;
}

static void juson_object_add(juson_value_t* obj, juson_value_t* pair)
{
    assert(pair->t == JUSON_PAIR);
    if (obj->head == NULL) {
        obj->head = pair;
        obj->tail = pair;
    } else {
        obj->tail->next = pair;
        obj->tail = obj->tail->next;
    }
}

char* juson_load(char* file_name)
{
    FILE* file = fopen(file_name, "rb");
    if (file == NULL)
        return NULL;
    
    size_t len;
    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* p = malloc(len + 1);
    if (p == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(p, len, 1, file) != 1) {
        free(p);
        fclose(file);
        return NULL;
    }
    p[len] = '\0';
    fclose(file);
    return p;
}

static void juson_chunk_init(juson_chunk_t* chunk)
{
    chunk->next = NULL;
    
    // Init slots
    juson_slot_t* slot = chunk->slots;
    for (int i = 0; i < JUSON_CHUNK_SIZE - 1; i++) {
        slot->next = slot + 1;
        slot = slot->next;
    }
    slot->next = NULL;
}

static void juson_pool_init(juson_pool_t* pool, int chunk_size)
{
    pool->allocated_n = 0;
    juson_chunk_init(&pool->head);
    pool->cur = pool->head.slots;
}

static juson_value_t* juson_alloc(juson_doc_t* doc)
{
    juson_pool_t* pool = &doc->pool;
    if (pool->cur == NULL) {
        juson_chunk_t* chunk = malloc(sizeof(juson_chunk_t));
        JUSON_EXPECT(chunk != NULL, "no memory");
        juson_chunk_init(chunk);
        pool->cur = chunk->slots;
        
        // Insert new chunk after the head
        chunk->next = pool->head.next;
        pool->head.next = chunk;        
    }
    pool->allocated_n++;
    juson_value_t* ret = &pool->cur->val;
    pool->cur = pool->cur->next;
    return ret;
}

void juson_destroy(juson_doc_t* doc)
{
    // Release memory
    free(doc->mem);
    doc->val = NULL;
    juson_value_t* p = doc->arr_list.next;
    while (p != NULL) {
        assert(p->data->t == JUSON_ARRAY);
        // The elements are allocated in pool.
        // Thus they shouldn't be freed.
        free(p->data->adata);
        p = p->next;
    }
    
    // Destroy pool
    juson_pool_t* pool = &doc->pool;
    juson_chunk_t* chunk = pool->head.next;
    while (chunk != NULL) {
        juson_chunk_t* tmp_next = chunk->next;
        free(chunk);
        chunk = tmp_next;
    }
}

juson_value_t* juson_parse(juson_doc_t* doc)
{
    doc->val = NULL;
    doc->p = doc->mem;
    doc->line = 1;
    doc->arr_list.t = JUSON_LIST;
    doc->arr_list.data = NULL;
    doc->arr_list.next = NULL;
    juson_pool_init(&doc->pool, 16);
    
    doc->val = juson_parse_value(doc);
    if (doc->val) {
        JUSON_EXPECT(doc->val->t == JUSON_OBJECT || doc->val->t == JUSON_ARRAY,
                "a JSON payload should be an object or array");
        JUSON_EXPECT(*doc->p == '\0', "unterminated");
    }
    return doc->val;
}

juson_value_t* juson_parse_string(juson_doc_t* doc, char* str)
{
    doc->mem = str;
    return juson_parse(doc);
}

/*
 * C/C++ style comment
 */
static char* juson_parse_comment(juson_doc_t* doc, char* p)
{
    if (p[0] == '*') {
        ++p;
        while (p[0] != '\0' && !(p[0] == '*' && p[1] == '/')) {
            if (p[0] == '\n')
                ++doc->line;
            ++p;
        }
        if (p[0] != '\0')
            p += 2;
    } else if (p[0] == '/'){
        while (p[0] != '\n' && p[0] != '\0')
            ++p;
        if (p[0] == '\n') {
            ++doc->line;
            ++p;
        }
    }
    return p;
}

static juson_value_t* juson_parse_object(juson_doc_t* doc)
{
    juson_value_t* obj = juson_new(doc, JUSON_OBJECT);
    if (obj == NULL) return NULL;
    if (try(doc, '}')) return obj;
        
    while (1) {
        JUSON_EXPECT(next(doc) == '\"', "expect '\"'");
        juson_value_t* pair = juson_parse_pair(doc);
        if (pair == NULL) return NULL;
        juson_object_add(obj, pair);
        char ch = next(doc);
        if (ch == '}') break;
        JUSON_EXPECT(ch == ',', "expect ','");
    }
    return obj;
}

static juson_value_t* juson_parse_pair(juson_doc_t* doc)
{
    juson_value_t* pair = juson_new(doc, JUSON_PAIR);
    if (pair == NULL)
        return NULL;
    pair->key = juson_parse_token(doc);
    if (pair->key == NULL)
        return NULL;
    JUSON_EXPECT(next(doc) == ':', "expect ':'");
    pair->val = juson_parse_value(doc);
    if (pair->val == NULL)
        return NULL;
    return pair;
}

static juson_value_t* juson_parse_value(juson_doc_t* doc)
{
    switch (next(doc)) {
    case '{': return juson_parse_object(doc);
    case '[': return juson_parse_array(doc);
    case '\"': return juson_parse_token(doc);
    case '0' ... '9': case '-': return juson_parse_number(doc);
    case 't': case 'f': return juson_parse_bool(doc);
    case 'n': return juson_parse_null(doc);
    case '\0': JUSON_EXPECT(0, "unexpect end of file");
    default:
        JUSON_EXPECT(0, "unexpect character");
    }
    return NULL; // Make compiler happy
}

static juson_value_t* juson_parse_null(juson_doc_t* doc)
{
    char* p = doc->p;
    if (p[0] == 'u' && p[1] == 'l' && p[2] == 'l') {
        doc->p = p + 3;
        return juson_new(doc, JUSON_NULL);
    }
    return NULL;
}

static juson_value_t* juson_parse_bool(juson_doc_t* doc)
{
    juson_value_t* b = juson_new(doc, JUSON_BOOL);
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
        JUSON_EXPECT(0, "expect 'true' or 'false'");
    }
    return b;
}

static juson_value_t* juson_parse_number(juson_doc_t* doc)
{
    char* begin = doc->p - 1;
    char* p = begin; // roll back
    if (p[0] == '-')
        ++p;
    if (p[0] == '0' && isdigit(p[1]))
        JUSON_EXPECT(0, "number leading by '0'");
    
    int digit_cnt = 0;
    int saw_dot = 0;
    int saw_e = 0;
    while (1) {
        switch (*p) {
        case '0' ... '9':
            ++digit_cnt;
            break;
        case '.':
            JUSON_EXPECT(!saw_e, "exponential term must be integer");
            JUSON_EXPECT(!saw_dot, "unexpected '.'");
            saw_dot = 1;
            break;
        case 'e':
        case 'E':
            JUSON_EXPECT(digit_cnt, "expect digit before 'e'");
            JUSON_EXPECT(!saw_e, "unexpected 'e'('E')");
            if (p[1] == '-' || p[1] == '+')
                ++p;
            saw_e = 1;
            digit_cnt = 0;
            break;
        default:
            goto ret;
        }
        p++;
    }
    
ret:
    JUSON_EXPECT(digit_cnt, saw_e ? "non digit after 'e'": "non digit in number");
    juson_value_t* val;
    if (saw_dot || saw_e) {
        val = juson_new(doc, JUSON_FLOAT);
        val->fval = atof(begin);
    } else {
        val = juson_new(doc, JUSON_INT);
        val->ival = atoi(begin);  
    }
    doc->p = p;
    return val;
}

static juson_value_t* juson_parse_array(juson_doc_t* doc)
{
    juson_value_t* arr = juson_new(doc, JUSON_ARRAY);
    if (arr == NULL) return NULL;
    if (juson_add_array(doc, arr) == NULL) return NULL;
    if (try(doc, ']')) return arr;
        
    while (1) {
        juson_value_t** ele = juson_array_push(arr);
        if (ele == NULL)
            return NULL;
        *ele = juson_parse_value(doc);
        if (*ele == NULL)
            return NULL;
        char ch = next(doc);
        if (ch == ']')
            break;
        JUSON_EXPECT(ch == ',', "expect ',' or ']'");
    }
    return arr;
}

static juson_value_t** juson_array_push(juson_value_t* arr)
{
    if (arr->size >= arr->capacity) {
        int new_c = arr->capacity * 2 + 1;
        arr->adata = (juson_value_t**)realloc(arr->adata,
                new_c * sizeof(juson_value_t*));
        if (arr->adata == NULL) {
            fprintf(stderr, "error: no memory");
            return NULL;
        }
        arr->capacity = new_c;
    }
    return &arr->adata[arr->size++];
}

static juson_value_t* juson_add_array(juson_doc_t* doc, juson_value_t* arr)
{
    assert(arr->t == JUSON_ARRAY);
    juson_value_t* node = juson_new(doc, JUSON_LIST);
    if (node == NULL)
        return NULL;
    node->data = arr;
    node->next = doc->arr_list.next;
    doc->arr_list.next = node;
    return node;
}

static juson_value_t* juson_parse_token(juson_doc_t* doc)
{
    juson_value_t* str = juson_new(doc, JUSON_STRING);
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
                    JUSON_EXPECT(isxdigit(*++p), "expect hexical");
                break;
            default:
                JUSON_EXPECT(0, "unexpected control label");
            }
            break;

        case '\b': case '\f':
        case '\n': case '\r': case '\t':
            JUSON_EXPECT(0, "unexpected control label");
        case '\"':
            *p = '\0';  // to simplify printf
            doc->p = p + 1;
            str->len = p - str->sdata;
            return str;
        case '\0':
            JUSON_EXPECT(0, "unexpected end of file, expect '\"'");    
        default: break;
        }
    }
    return str; // Make compiler happy
}

juson_value_t* juson_object_get(juson_value_t* obj, char* name)
{
    if (obj->t != JUSON_OBJECT)
        return NULL;
    juson_value_t* p = obj->head;
    while (p != NULL) {
        if (strncmp(p->key->sdata, name, p->key->len) == 0)
            return p->val;
        p = p->next;
    }
    return NULL;
}

juson_value_t* juson_array_get(juson_value_t* arr, size_t idx)
{
    if (arr->t != JUSON_OBJECT) return NULL;
    if (idx >= arr->size) return NULL;
    return arr->adata[idx];
}
