#include "juson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    if (argc < 2)
        return -1;

    juson_doc_t doc;
    if (juson_load(&doc, argv[1]) == JUSON_ERR) {
        return -1;
    }
    
    juson_value_t* obj = juson_parse(&doc);
    if (obj == NULL)
        return  -1;
    assert(obj->t == JUSON_OBJECT);
    juson_value_t* pair = obj->head;
    while (pair != NULL) {
        printf("%s: ", pair->key->sdata);
        juson_value_t* val = pair->val;
        switch (val->t) {
        case JUSON_STRING:
            printf("%s", val->sdata);
            break;
        case JUSON_INT:
            printf("%d", val->ival);
            break;
        case JUSON_FLOAT:
            printf("%f", val->fval);
            break;
        case JUSON_ARRAY:
            for (int i = 0; i < val->size; i++) {
                if (val->adata[i]->t == JUSON_STRING)
                    printf("%s, ", val->adata[i]->sdata);
            }
            break;
        default:
            break;
        }
        printf("\n");
        pair = pair->next;
    }
   
   juson_destroy(&doc);
    return 0;
}
