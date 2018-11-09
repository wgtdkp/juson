#include "juson.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JUSON_FAIL(v) {     \
    juson_free_value(v);    \
    return NULL;            \
};

#define JUSON_EXPECT(v, cond, msg) {                        \
    if (!(cond)) {                                          \
        fprintf(stderr, "%s: %d: ", __func__, __LINE__);    \
        juson_error(doc, msg);                              \
        JUSON_FAIL(v);                                      \
    };                                                      \
};

#if JUSON_ERR_HINT
static void juson_error(juson_doc_t* doc, const char* format, ...) {
    fprintf(stderr, "error: line %d: ", doc->line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}
#else
#define juson_error(doc, format, ...)
#endif

static int next(juson_doc_t* doc);
static int try(juson_doc_t* doc, char x);
static juson_value_t* juson_new(juson_doc_t* doc, juson_type_t t);
static void juson_object_add(juson_value_t* obj, juson_value_t* pair);
static void juson_pool_init(juson_pool_t* pool);
static juson_value_t* juson_alloc(juson_doc_t* doc);
static const char* juson_parse_comment(juson_doc_t* doc, const char* p);
static juson_value_t* juson_parse_object(juson_doc_t* doc);
static juson_value_t* juson_parse_pair(juson_doc_t* doc);
static juson_value_t* juson_parse_value(juson_doc_t* doc);
static juson_value_t* juson_parse_null(juson_doc_t* doc);
static juson_value_t* juson_parse_bool(juson_doc_t* doc);
static juson_value_t* juson_parse_number(juson_doc_t* doc);
static juson_value_t* juson_parse_array(juson_doc_t* doc);
static inline juson_value_t** juson_array_push(juson_value_t* arr);
static juson_value_t* juson_parse_string(juson_doc_t* doc);
static char* juson_write_utf8(juson_doc_t* doc, char* p, uint32_t val);
static void juson_free_value(juson_value_t* v);

static inline int xdigit(int c) {
    switch (c) {
    case '0' ... '9': return c - '0';
    case 'a' ... 'f': return c - 'a' + 10;
    case 'A' ... 'F': return c - 'A' + 10;
    default: assert(false);
    }
    return 0;
}

static inline uint32_t ucs(const char* p) {
    return (xdigit(p[0]) << 12) + (xdigit(p[1]) << 8) +
           (xdigit(p[2]) << 4) + xdigit(p[3]);
}


static int next(juson_doc_t* doc) {
    const char* p = doc->p;
    while (1) {
        while (*p == ' ' || *p == '\t' ||
               *p == '\r' || *p == '\n') {
            if (*p == '\n') {
                ++doc->line;
            }
            ++p;
        }
        if (*p != '/') {
            break;
        }
        p = juson_parse_comment(doc, p + 1);
    }
    doc->p = p;
    if (*p != '\0') {
        ++doc->p;
    }
    return *p;
}

static int try(juson_doc_t* doc, char x) {
    // Check empty array
    const char* p = doc->p;
    int line = doc->line;
    char c = next(doc);
    if (c == x) {
        return 1;
    }
    doc->p = p;
    doc->line = line;
    return 0;
}

static juson_value_t* juson_new(juson_doc_t* doc, juson_type_t t) {
    juson_value_t* val = juson_alloc(doc);
    memset(val, 0, sizeof(juson_value_t));
    val->t = t;
    return val;
}

static void juson_object_add(juson_value_t* obj, juson_value_t* pair) {
    assert(pair->t == JUSON_PAIR);
    if (obj->head == NULL) {
        obj->head = pair;
        obj->tail = pair;
    } else {
        obj->tail->next = pair;
        obj->tail = obj->tail->next;
    }
}

char* juson_load(const char* file_name) {
    FILE* file = fopen(file_name, "rb");
    if (file == NULL) {
        return NULL;
    }
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

static void juson_chunk_init(juson_chunk_t* chunk) {
    chunk->next = NULL;

    // Init slots
    juson_slot_t* slot = chunk->slots;
    for (int i = 0; i < JUSON_CHUNK_SIZE - 1; ++i) {
        slot->next = slot + 1;
        slot = slot->next;
    }
    slot->next = NULL;
}

static void juson_pool_init(juson_pool_t* pool) {
    pool->allocated_n = 0;
    juson_chunk_init(&pool->head);
    pool->cur = pool->head.slots;
}

static juson_value_t* juson_alloc(juson_doc_t* doc) {
    juson_pool_t* pool = &doc->pool;
    if (pool->cur == NULL) {
        juson_chunk_t* chunk = malloc(sizeof(juson_chunk_t));
        juson_chunk_init(chunk);
        pool->cur = chunk->slots;

        // Insert new chunk after the head
        chunk->next = pool->head.next;
        pool->head.next = chunk;
    }
    ++pool->allocated_n;
    juson_value_t* ret = &pool->cur->val;
    pool->cur = pool->cur->next;
    return ret;
}

void juson_destroy(juson_doc_t* doc) {
    juson_free_value(doc->val);
    doc->val = NULL;

    // Clear pool
    juson_pool_t* pool = &doc->pool;
    juson_chunk_t* chunk = pool->head.next;
    while (chunk != NULL) {
        juson_chunk_t* tmp_next = chunk->next;
        free(chunk);
        chunk = tmp_next;
    }
}

static void juson_free_value(juson_value_t* v) {
    if (v == NULL) {
        return;
    }
    switch (v->t) {
    case JUSON_OBJECT:
        for (juson_value_t* kid = v->head; kid != NULL; kid = kid->next) {
            juson_free_value(kid);
        }
        break;
    case JUSON_INTEGER:
    case JUSON_FLOAT:
    case JUSON_BOOL:
    case JUSON_NULL:
        break;
    case JUSON_PAIR:
        juson_free_value(v->key);
        juson_free_value(v->val);
        break;
    case JUSON_ARRAY:
        for (int i = 0; i < v->size; ++i) {
            juson_free_value(v->adata[i]);
        }
        free(v->adata);
        break;
    case JUSON_STRING:
        if (v->need_free) {
            free((char*)v->sval);
        }
        break;
    default:
        assert(false);
    }
}

juson_value_t* juson_parse(juson_doc_t* doc, const char* json) {
    doc->val = NULL;
    doc->p = json;
    doc->line = 1;
    juson_pool_init(&doc->pool);
    juson_value_t* root = juson_parse_value(doc);
    if (root) {
        JUSON_EXPECT(root, root->t == JUSON_OBJECT || root->t == JUSON_ARRAY,
                     "a JSON payload should be an object or array");
        JUSON_EXPECT(root, next(doc) == '\0', "unterminated");
    }
    doc->val = root;
    return doc->val;
}

/*
 * C/C++ style comment
 */
static const char* juson_parse_comment(juson_doc_t* doc, const char* p) {
    if (p[0] == '*') {
        ++p;
        while (p[0] != '\0' && !(p[0] == '*' && p[1] == '/')) {
            if (p[0] == '\n') {
                ++doc->line;
            }
            ++p;
        }
        if (p[0] != '\0') {
            p += 2;
        }
    } else if (p[0] == '/'){
        while (p[0] != '\n' && p[0] != '\0') {
            ++p;
        }
        if (p[0] == '\n') {
            ++doc->line;
            ++p;
        }
    }
    return p;
}

static juson_value_t* juson_parse_object(juson_doc_t* doc) {
    juson_value_t* obj = juson_new(doc, JUSON_OBJECT);
    if (try(doc, '}')) {
        return obj;
    }
    while (1) {
        JUSON_EXPECT(obj, next(doc) == '\"', "expect '\"'");
        juson_value_t* pair = juson_parse_pair(doc);
        if (pair == NULL) {
            JUSON_FAIL(obj);
        }
        juson_object_add(obj, pair);
        char c = next(doc);
        if (c == '}') {
            break;
        }
        JUSON_EXPECT(obj, c == ',', "expect ','");
    }
    return obj;
}

static juson_value_t* juson_parse_pair(juson_doc_t* doc) {
    juson_value_t* pair = juson_new(doc, JUSON_PAIR);
    pair->key = juson_parse_string(doc);
    if (pair->key == NULL) {
        JUSON_FAIL(pair);
    }
    JUSON_EXPECT(pair, next(doc) == ':', "expect ':'");
    pair->val = juson_parse_value(doc);
    if (pair->val == NULL) {
        JUSON_FAIL(pair);
    }
    return pair;
}

static juson_value_t* juson_parse_value(juson_doc_t* doc) {
    switch (next(doc)) {
    case '{': return juson_parse_object(doc);
    case '[': return juson_parse_array(doc);
    case '\"': return juson_parse_string(doc);
    case '0' ... '9': case '-': return juson_parse_number(doc);
    case 't': case 'f': return juson_parse_bool(doc);
    case 'n': return juson_parse_null(doc);
    case '\0': juson_error(doc, "premature end of file");
    default: juson_error(doc, "unexpect character");
    }
    return NULL; // Make compiler happy
}

static juson_value_t* juson_parse_null(juson_doc_t* doc) {
    const char* p = doc->p;
    if (p[0] == 'u' && p[1] == 'l' && p[2] == 'l') {
        doc->p = p + 3;
        return juson_new(doc, JUSON_NULL);
    }
    return NULL;
}

static juson_value_t* juson_parse_bool(juson_doc_t* doc) {
    juson_value_t* b = juson_new(doc, JUSON_BOOL);

    const char* p = doc->p - 1;
    if (p[0] == 't' && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
        b->bval = 1;
        doc->p = p + 4;
    } else if (p[0] == 'f' && p[1] == 'a' &&
               p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
        b->bval = 0;
        doc->p = p + 5;
    } else {
        JUSON_EXPECT(b, 0, "expect 'true' or 'false'");
    }
    return b;
}

static juson_value_t* juson_parse_number(juson_doc_t* doc) {
    const char* begin = doc->p - 1;
    const char* p = begin; // roll back
    if (p[0] == '-') {
        ++p;
    }
    if (p[0] == '0' && isdigit(p[1]))
        JUSON_EXPECT(NULL, 0, "number leading by '0'");

    int digit_cnt = 0;
    int saw_dot = 0;
    int saw_e = 0;
    while (1) {
        switch (*p) {
        case '0' ... '9':
            ++digit_cnt;
            break;
        case '.':
            JUSON_EXPECT(NULL, digit_cnt, "expect digit before '.'");
            JUSON_EXPECT(NULL, !saw_e, "exponential term must be integer");
            JUSON_EXPECT(NULL, !saw_dot, "unexpected '.'");
            saw_dot = 1;
            digit_cnt = 0;
            break;
        case 'e':
        case 'E':
            JUSON_EXPECT(NULL, digit_cnt, "expect digit before 'e'");
            JUSON_EXPECT(NULL, !saw_e, "unexpected 'e'('E')");
            if (p[1] == '-' || p[1] == '+') {
                ++p;
            }
            saw_e = 1;
            digit_cnt = 0;
            break;
        default:
            goto ret;
        }
        ++p;
    }

ret:
    JUSON_EXPECT(NULL, digit_cnt, "non digit after 'e'/'.'");
    juson_value_t* val;
    if (saw_dot || saw_e) {
        val = juson_new(doc, JUSON_FLOAT);
        val->fval = atof(begin);
    } else {
        val = juson_new(doc, JUSON_INTEGER);
        val->ival = atol(begin);
    }
    doc->p = p;
    return val;
}

static juson_value_t* juson_parse_array(juson_doc_t* doc) {
    juson_value_t* arr = juson_new(doc, JUSON_ARRAY);
    if (try(doc, ']')) {
        return arr;
    }
    while (true) {
        juson_value_t** ele = juson_array_push(arr);
        *ele = juson_parse_value(doc);
        if (*ele == NULL) {
            JUSON_FAIL(arr);
        }
        char c = next(doc);
        if (c == ']') {
            break;
        }
        JUSON_EXPECT(arr, c == ',', "expect ',' or ']'");
    }
    return arr;
}

static inline juson_value_t** juson_array_push(juson_value_t* arr) {
    if (arr->size >= arr->capacity) {
        int new_c = arr->capacity * 2 + 1;
        juson_value_t** tmp = arr->adata;
        arr->adata = malloc(new_c * sizeof(juson_value_t*));
        memcpy(arr->adata, tmp, arr->capacity * sizeof(juson_value_t*));
        free(tmp);
        arr->capacity = new_c;
    }
    return &arr->adata[arr->size++];
}

static juson_value_t* juson_parse_string(juson_doc_t* doc) {
    juson_value_t* str = juson_new(doc, JUSON_STRING);
    str->sval = doc->p;
    bool need_copy = false;
    const char* p = doc->p;
    for (; ; ++p) {
        int c = *p;
        switch (c) {
        case '\\':
            switch (*++p) {
            case '"': case '\\': case '/':
            case 'b': case 'f': case 'n':
            case 'r': case 't':
                need_copy = true;
                break;
            case 'u':
                for (int i = 0; i < 4; ++i) {
                    // Don't use expression that has side-effect in macro
                    bool is_xdigit = isxdigit(*++p);
                    JUSON_EXPECT(str, is_xdigit, "expect hexical");
                }
                need_copy = true;
                break;
            default:
                JUSON_EXPECT(str, 0, "unexpected control label");
            }
            break;
        case '\b': case '\n': case '\r': case '\t':
            JUSON_EXPECT(str, 0, "unexpected control label");
        case '\"':
            doc->p = p + 1;
            str->len = p - str->sval;
            goto end_of_loop;
        case '\0':
            JUSON_EXPECT(str, 0, "unexpected end of file, expect '\"'");
        default: break;
        }
    }

end_of_loop:
    if (!need_copy) {
        return str;
    }
    str->need_free = true;
    char* q = malloc(str->len + 1);
    p = str->sval;
    str->sval = q;
    for (int i = 0; i < str->len; ++i) {
        int c = p[i];
        if (c == '\\') {
            switch (p[++i]) {
            case '"': *q++ = '\"'; break;
            case '\\': *q++ = '\\'; break;
            case '/': *q++ = '/'; break;
            case 'b': *q++ = '\b'; break;
            case 'n': *q++ = '\n'; break;
            case 'r': *q++ = '\r'; break;
            case 't': *q++ = '\t'; break;
                break;
            case 'u': {
                uint32_t val = ucs(&p[i + 1]);
                if (val >= 0xd800 && val < 0xe000) {
                    JUSON_EXPECT(str, p[i + 5] == '\\', "invalid UCS");
                    JUSON_EXPECT(str, p[i + 6] == 'u', "invalid UCS");
                    uint32_t val_tail = 0x03ff & (ucs(&p[i + 7]) - 0xdc00);
                    val = (((val - 0xd800) << 10) | val_tail) + 0x10000;
                    i += 6;
                }
                q = juson_write_utf8(doc, q, val);
                i += 4;
            } break;
            default: assert(false);
            }
        } else {
            *q++ = c;
        }
    }
    *q = '\0';
    str->len = q - str->sval;
    return str;
}

static char* juson_write_utf8(juson_doc_t* doc, char* p, uint32_t val) {
    if (val < 0x80) {
        *p++ = val & 0x7f;
    } else if (val < 0x800) {
        *p++ = 0xc0 | ((val >> 6) & 0x1f);
        *p++ = 0x80 | (val & 0x3f);
    } else if (val < 0x10000) {
        *p++ = 0xe0 | ((val >> 12) & 0x0f);
        *p++ = 0x80 | ((val >> 6) & 0x3f);
        *p++ = 0x80 | (val & 0x3f);
    } else if (val < 0x200000) {
        *p++ = 0xf0 | ((val >> 18) & 0x07);
        *p++ = 0x80 | ((val >> 12) & 0x3f);
        *p++ = 0x80 | ((val >> 6) & 0x3f);
        *p++ = 0x80 | (val & 0x3f);
    } else if (val < 0x4000000) {
        *p++ = 0xf8 | ((val >> 24) & 0x03);
        *p++ = 0x80 | ((val >> 18) & 0x3f);
        *p++ = 0x80 | ((val >> 12) & 0x3f);
        *p++ = 0x80 | ((val >> 6) & 0x3f);
        *p++ = 0x80 | (val & 0x3f);
    } else if (val < 0x80000000) {
        *p++ = 0xfc | ((val >> 30) & 0x01);
        *p++ = 0x80 | ((val >> 24) & 0x3f);
        *p++ = 0x80 | ((val >> 18) & 0x3f);
        *p++ = 0x80 | ((val >> 12) & 0x3f);
        *p++ = 0x80 | ((val >> 6) & 0x3f);
        *p++ = 0x80 | (val & 0x3f);
    } else {
        JUSON_EXPECT(NULL, false, "invalid UCS")
    }
    return p;
}

juson_value_t* juson_object_get(juson_value_t* obj, char* name) {
    if (obj->t != JUSON_OBJECT) {
        return NULL;
    }
    juson_value_t* p = obj->head;
    while (p != NULL) {
        if (strncmp(p->key->sval, name, p->key->len) == 0) {
            return p->val;
        }
        p = p->next;
    }
    return NULL;
}

juson_value_t* juson_array_get(juson_value_t* arr, size_t idx) {
    if (arr->t != JUSON_ARRAY || idx >= arr->size) {
        return NULL;
    }
    return arr->adata[idx];
}
