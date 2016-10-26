#include "juson.h"


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char* argv[]) {
    if (argc < 2)
        return -1;

    juson_doc_t doc;
    const char* json = juson_load(argv[1]);
    if (json == NULL) {
        return -1;
    }
    printf("sizeof(juson_value_t): %lu \n", sizeof(juson_value_t));
    printf("begin parsing...\n");
    clock_t begin = clock();
    juson_value_t* val = juson_parse(&doc, json);
    printf("parse done\n");
    if (val == NULL) {
        printf("parse failed\n");
        return -1;
    }
    printf("parse time: %f\n", (clock() - begin) * 1.0f / CLOCKS_PER_SEC);
    printf("memory consumption: %ld \n", doc.pool.allocated_n * sizeof(juson_value_t));
    juson_destroy(&doc);
    return 0;
}
