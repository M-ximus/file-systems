#include <stdlib.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <errno.h>
#include <string.h>

#define PRINT_ERROR(str) do {perror(str); return EXIT_FAILURE;} while(0);

int main(int argc, char* argv[])
{
    if (argc < 2)
        PRINT_ERROR("Not enought arguments for interesting test\n");

    printf("First argv = %s\n", argv[0]);

    //size_t argv0_len = strlen(argv[0]);

    printf("%p\n", argv[0]);
    printf("%p\n", argv[1]);
    int ret = prctl(PR_SET_MM, PR_SET_MM_ARG_START, (unsigned long)argv[1] , 0, 0);
    //if (ret < 0)
        //PRINT_ERROR("Set new stack value returned error\n");
    printf("Slice stack\n");

    FILE* in = fopen("/proc/self/cmdline", "r");
    if (in == NULL)
        PRINT_ERROR("can't open file\n");

    char test[400] = {};
    fscanf(in, "%400s", test);
    fclose(in);

    printf("First argv = %s\n", argv[0]);
    printf("First in cmdline = %s\n", test);

    return 0;
}
