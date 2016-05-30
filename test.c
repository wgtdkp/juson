#include "qson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    if (argc < 2)
        return -1;

    qson_doc_t doc;
    if (qson_load(&doc, argv[1]) == QSON_ERR) {
        return -1;
    }
    
    qson_value_t* obj = qson_parse(&doc);
    if (obj == NULL)
        return  -1;
    assert(obj->t == QSON_OBJECT);
    qson_value_t* pair = obj->head;
    while (pair != NULL) {
        printf("%s: ", pair->key->sdata);
        qson_value_t* val = pair->val;
        switch (val->t) {
        case QSON_STRING:
            printf("%s", val->sdata);
            break;
        case QSON_INT:
            printf("%d", val->ival);
            break;
        case QSON_FLOAT:
            printf("%f", val->fval);
            break;
        case QSON_ARRAY:
            for (int i = 0; i < val->size; i++) {
                if (val->adata[i]->t == QSON_STRING)
                    printf("%s, ", val->adata[i]->sdata);
            }
            break;
        default:
            break;
        }
        printf("\n");
        pair = pair->next;
    }
   
   qson_destroy(&doc);
    return 0;
}
