#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>

#define PRINT_ERROR(str) do {perror(str); return EXIT_FAILURE;} while(0);

int get_fd_folder_path(char* dest, size_t dest_size, const char* pid_str);
int print_opened_fds(const char* fd_path);

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr,
                "Bad number of args = %d. Try ./a.out num_threads\n", argc);
        return EXIT_FAILURE;
    }

    errno = 0;
    long int pid = strtol(argv[1], NULL, 10);
    if (errno < 0)
        PRINT_ERROR("Bad pid\n");

    errno = 0;
    char fd_folder[PATH_MAX] = {};
    errno = get_fd_folder_path(fd_folder, PATH_MAX, argv[1]);
    if (errno != 0)
        PRINT_ERROR("Can't transform pid to path to folder with pid fd's\n");

    errno = print_opened_fds(fd_folder);
    if (errno != 0)
        PRINT_ERROR("Can't print opened fds of process");

    return 0;
}

int get_fd_folder_path(char* dest, size_t dest_size, const char* pid_str)
{
    if (dest == NULL || pid_str == NULL)
        PRINT_ERROR("[get_fd_folder_path] Bad input pointers\n");

    size_t src_len = strnlen(pid_str, dest_size);
    if (dest_size < src_len + 10) // /proc/[pid]/fd + \0
        PRINT_ERROR("[get_fd_folder_path] too small destination str\n");

    strncpy(dest, "/proc/", 7);
    dest[6] = '\0';

    strncat(dest, pid_str, src_len);
    dest[src_len  + 6] = '\0';

    strncat(dest, "/fd", 4);
    dest[dest_size - 1] = '\0';

    return 0;
}

int print_opened_fds(const char* fd_path)
{
    if (fd_path == NULL)
        PRINT_ERROR("[print_opened_fds] Bad input fd_path\n");

    errno = 0;
    int dir_fd = open(fd_path, O_RDONLY);
    if (dir_fd < 0)
        PRINT_ERROR("[print_opened_fds] Open dir returned error\n");

    errno = 0;
    DIR* proc_dir = fdopendir(dir_fd);
    if (errno < 0)
        PRINT_ERROR("[print_opened_fds] Open dir stream by dir fd failed\n");

    errno = 0;
    struct dirent* entry_ptr = readdir(proc_dir);
    if (errno < 0)
        PRINT_ERROR("[print_opened_fds] Init readdir returned error");

    printf("  inode_ID   UID       SIZE    NAME\n");
    while(entry_ptr != NULL)
    {
        if (entry_ptr->d_type == DT_LNK)
        {
            char buff[PATH_MAX] = {};
            errno = 0;
            ssize_t readed = readlinkat(dir_fd, entry_ptr->d_name,
                                        buff, PATH_MAX);
            if (readed < 0)
            {
                closedir(proc_dir);
                close(dir_fd);
                PRINT_ERROR("[print_opened_fds] readlink failed\n");
            }

            struct stat node_stat;
            fstatat(dir_fd, entry_ptr->d_name, &node_stat, 0);
            printf("%10ld %5d %10ld %s\n",
                   node_stat.st_ino, node_stat.st_uid, node_stat.st_size, buff);
        }

        errno = 0;
        entry_ptr = readdir(proc_dir);
    }

    closedir(proc_dir);
    close(dir_fd);

    return 0;
}
