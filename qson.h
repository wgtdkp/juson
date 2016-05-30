#ifndef _QSON_H_
#define _QSON_H_

#include <stdio.h>

#define QSON_OK     (0)
#define QSON_ERR    (-1)


typedef enum   qson_type    qson_type_t;
typedef struct qson_value   qson_value_t;
typedef struct qson_pool    qson_pool_t;
typedef int                 qson_int_t;
typedef float               qson_float_t;
typedef unsigned char       qson_bool_t;

enum qson_type {
    QSON_OBJECT,
    QSON_INT,
    QSON_FLOAT,
    QSON_ARRAY,
    QSON_BOOL,
    QSON_STRING,
    QSON_NULL,
    QSON_PAIR,
    QSON_LIST,
};


struct qson_value {
    qson_type_t t;
    union {
        qson_int_t ival;
        qson_float_t fval;
        qson_bool_t bval;
        
        // String
        struct {
            char* sdata;
            int len;  
        };
        
        // Array
        struct {
            int size;
            int capacity;
            qson_value_t** adata;
        };
        
        // Object
        struct {
            qson_value_t* tail; // pair
            qson_value_t* head; // pair
        };
        
        struct {
            // Pair
            struct {
                qson_value_t* key; // string
                qson_value_t* val;
            };
        
            // List
            struct {
                qson_value_t* data;
            };
            qson_value_t* next; // pair
        };
    };
};

struct qson_pool {
    int chunk_size;
    int allocated_n;
    qson_value_t* cur;
    qson_value_t chunk_arr;
};

typedef struct {
    FILE* file;
    char* mem;
    char* p;
    
    int line;
    int ele_num;
    
    qson_value_t* obj;
    qson_value_t arr_list;
    
    qson_pool_t pool;
} qson_doc_t;

int qson_load(qson_doc_t* doc, char* file_name);
qson_value_t* qson_parse(qson_doc_t* doc);
qson_value_t* qson_parse_string(qson_doc_t* doc, char* str);
void qson_destroy(qson_doc_t* doc);;
#endif
