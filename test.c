#include "juson.h"


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


int main(int argc, char* argv[])
{
    if (argc < 2)
        return -1;

    juson_doc_t json;
    if ((json.mem = juson_load(argv[1])) == NULL) {
        return -1;
    }
    printf("sizeof(juson_value_t): %d \n", sizeof(juson_value_t));
    printf("begin parsing...\n");
    clock_t begin = clock();
    juson_value_t* obj = juson_parse(&json);
    printf("parse done\n");
    if (obj == NULL) {
        printf("parse failed\n");
        return -1;
    }
    printf("parse time: %f\n", (clock() - begin) * 1.0f / CLOCKS_PER_SEC);
    juson_destroy(&json);
    return 0;
    
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
   
   juson_destroy(&json);
    return 0;
}
