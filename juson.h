#ifndef _JUSON_H_
#define _JUSON_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JUSON_ERR_HINT      (0)
#define JUSON_OK            (0)
#define JUSON_ERR           (-1)
#define JUSON_CHUNK_SIZE    (128)

typedef struct juson_value  juson_value_t;
typedef long                juson_int_t;
typedef double              juson_float_t;
typedef unsigned char       juson_bool_t;

typedef enum juson_type {
    JUSON_OBJECT,
    JUSON_INTEGER,
    JUSON_FLOAT,
    JUSON_ARRAY,
    JUSON_BOOL,
    JUSON_STRING,
    JUSON_NULL,
    JUSON_PAIR,
    JUSON_LIST,
} juson_type_t;

struct juson_value {
    juson_type_t t;
    union {
        juson_int_t ival;
        juson_float_t fval;
        juson_bool_t bval;
        
        // String
        struct {
            const char* sval;
            int need_free;
            int len;
        };
        
        // Array
        struct {
            int size;
            int capacity;
            juson_value_t** adata;
        };
        
        // Object
        struct {
            juson_value_t* tail; // pair
            juson_value_t* head; // pair
        };
        
        struct {
            union {
                // Pair
                struct {
                    juson_value_t* key; // string
                    juson_value_t* val;
                };
                // List
                juson_value_t* data;
            };
            juson_value_t* next;
        };
    };
};

typedef union  juson_slot   juson_slot_t;
typedef struct juson_chunk  juson_chunk_t;
typedef struct juson_pool   juson_pool_t;

union juson_slot {
    juson_slot_t* next;
    juson_value_t val;
};

struct juson_chunk {
    juson_chunk_t* next;
    juson_slot_t slots[JUSON_CHUNK_SIZE];
};

struct juson_pool {
    int allocated_n;
    juson_slot_t* cur;
    juson_chunk_t head;
};

typedef struct {
    const char* p;    
    int line;
    juson_value_t* val;
    juson_pool_t pool;
} juson_doc_t;

char* juson_load(const char* file_name);
juson_value_t* juson_parse(juson_doc_t* doc, const char* json);
void juson_destroy(juson_doc_t* doc);
juson_value_t* juson_object_get(juson_value_t* obj, char* name);
juson_value_t* juson_array_get(juson_value_t* arr, size_t idx);

#ifdef __cplusplus
}
#endif

#endif
