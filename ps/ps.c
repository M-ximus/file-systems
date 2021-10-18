#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PRINT_ERROR(str) do {perror(str); return EXIT_FAILURE;} while(0);

int is_piddir(const char* dir_name);
int get_stat_path(char* file_path, size_t buf_size, const char* dir_name);
int print_stat(const char* stat_file_path);

int main(int argc, char* argv[])
{
    errno = 0;
    DIR* proc_dir = opendir("/proc");
    if (errno < 0)
        PRINT_ERROR("[main] Open /proc failed\n");

    errno = 0;
    struct dirent* entry_ptr = readdir(proc_dir);
    if (errno < 0)
        PRINT_ERROR("[main] Init readdir returned error");


    printf("pid    status ppid  name\n");
    while(entry_ptr != NULL)
    {
        if (entry_ptr->d_type == DT_DIR)
        {
            if (is_piddir(entry_ptr->d_name))
            {
                char stat_file_path[PATH_MAX] = {};
                int err = get_stat_path(stat_file_path, PATH_MAX,
                                        entry_ptr->d_name);
                if (err != 0)
                    PRINT_ERROR("[main] produce path to stat file returned error\n");

                err = print_stat(stat_file_path);
                if (err != 0)
                    PRINT_ERROR("[main] print stat info returned error\n");
            }
        }
        errno = 0;
        entry_ptr = readdir(proc_dir);
    }
    if (errno < 0)
        PRINT_ERROR("[main] Read next entry returned error\n");

    closedir(proc_dir);
    return 0;
}

int is_piddir(const char* dir_name)
{
    if (dir_name == NULL)
    {
        fprintf(stderr, "[is_piddir] Null iput string\n");
        return 0;
    }

    errno = 0;
    char* endptr = NULL;
    long int pid = strtol(dir_name, &endptr, 10);
    if (errno < 0)
    {
        perror("[is_piddir] Error in strtol of dirname\n");
        return 0;
    }

    if (endptr == dir_name)
        return 0;

    if (*endptr != '\0')
        return 0;

    return 1;
}

int get_stat_path(char* file_path, size_t buf_size, const char* dir_name)
{
    // "/proc/"
    if (buf_size < 7)
        PRINT_ERROR("[get_stat_path] Not enough space for proc folder");
    if (file_path == NULL || dir_name == NULL)
        PRINT_ERROR("[get_stat_path] Bad input pointer");

    size_t stop = buf_size - 7 - 5; // "/proc/*/stat"
    if (stop > PATH_MAX)
        stop = PATH_MAX;

    size_t dir_name_len = strnlen(dir_name, stop);
    if (dir_name_len == stop)
        PRINT_ERROR("[get_stat_path] string buff is too short\n");

    strncpy(file_path, "/proc/", 7);
    file_path[6] = '\0';

    strncat(file_path, dir_name, stop);
    file_path[7 + stop - 1] = '\0';

    strncat(file_path, "/stat", 6);
    file_path[stop + 11] = '\0';
    //printf("%s\n", file_path);

    return 0;
}

int print_stat(const char* stat_file_path)
{
    if (stat_file_path == NULL)
        PRINT_ERROR("[print_stat] Null input pointer\n");

    errno = 0;
    FILE* stat_ptr = fopen(stat_file_path, "r");
    if (stat_ptr == NULL)
        PRINT_ERROR("[print_stat] Can't open input file\n");

    int pid = 0;
    char comm[PATH_MAX] = {};
    char test[PATH_MAX] = {};
    char state = 0;
    int ppid = 0;

    int num_scaned = fscanf(stat_ptr, "%d %[^)]%[^ ] %c %d", &pid, comm, test,
                            &state, &ppid);

    fclose(stat_ptr);
    if (num_scaned != 5)
        //num_scaned = fscanf(stat_ptr, "%d %s %c %d", &pid, comm, &state, &ppid);
        PRINT_ERROR("[print_stat] Can't scan stat file\n");

    printf("%5d  %c  %5d     %s\n", pid, state, ppid, comm + 1);

    return 0;
}
