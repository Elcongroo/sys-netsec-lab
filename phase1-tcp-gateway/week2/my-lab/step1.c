#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){
    char* buf = (char*)malloc(8);
    strcpy(buf, "this string is too long");
    printf("buf = %s\n", buf);
    free(buf);
    return 0;
}