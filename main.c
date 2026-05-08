#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <magic.h>

#define MEMORY_CHECK_USE_AFTER_FREE 0
#include "util.c"
#include "memory.c"

static struct termios original_term_attrs;
#define MAX_TERM_RESPONSE_LEN 256
static char *index0 = "[0]";

static void __attribute((noreturn))
cleanup(int signal_number) {
    (void)signal_number;
    tcsetattr(STDIN_FILENO, TCSANOW, &original_term_attrs);
    printf("\033\\");
    exit(EXIT_SUCCESS);
}

typedef struct FileName {
    char *path;
    int32 len;
} FileName;

static int32
read_term_response(char *sequence, char end_character, char *output_buffer) {
    static double timeout_seconds = 0.01;
    struct timeval timeout;
    fd_set read_fds;
    int32 index = 0;
    int32 sequence_len = strlen32(sequence);
    write64(STDERR_FILENO, sequence, sequence_len);
    
    while (1) {
        int32 selected;
        char current_char;
        int64 read_bytes;

        timeout.tv_sec = (int32)timeout_seconds;
        timeout.tv_usec = (int32)((timeout_seconds - (int32)timeout_seconds)*1000000);
        
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        
        selected = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        if (selected <= 0) {
            error("Terminal didnt answer for the sequence\n");
            for (int32 i = 0; i < sequence_len; i += 1) {
                fprintf(stderr, "%c ", sequence[i]);
            }
            error("\n");
            break;
        }
        
        read_bytes = read(STDIN_FILENO, &current_char, 1);
        if (read_bytes <= 0) {
            break;
        }
        
        if (index < MAX_TERM_RESPONSE_LEN - 1) {
            output_buffer[index] = current_char;
            index += 1;
        }
        
        if (current_char == end_character) {
            break;
        }
    }
    
    output_buffer[index] = '\0';
    return index;
}

static int32
compare_filenames(const void *a, const void *b) {
    FileName *file_a = (FileName *)a;
    FileName *file_b = (FileName *)b;
    
    return strcmp(file_a->path, file_b->path);
}

int main(int argc, char **argv) {
    struct termios raw_term_attrs;
    char term_reply[MAX_TERM_RESPONSE_LEN];
    char *TERM;
    bool has_sixel = false;
    char *force_sixel = getenv("LSIX_FORCE_SIXEL_SUPPORT");
    int32 num_colors = 16;
    char background[64];
    char foreground[64];

    int32 screen_width = 800;
    int32 tile_size = 120;
    int32 tile_width = tile_size;
    int32 tile_height = tile_size;
    int32 font_size = tile_width / 10;

    int32 parsed_colors = 0;
    
    char *font_family = "Dejavu-Sans";

    int32 tile_x_space = screen_width / 201;
    int32 tile_y_space = tile_x_space / 2;
    int32 width_denominator = tile_width + 2*tile_x_space + 1;
    int32 num_tiles = screen_width / width_denominator;

    char error_file[] = "/tmp/lsixc-XXXXXX";
    int32 error_fd;

    FileName *list = NULL;
    int32 list_len = 0;

    if ((error_fd = mkstemp(error_file)) < 0) {
        error("Error in mkstemp: %s.\n", strerror(errno));
        fatal(EXIT_FAILURE);
    }

    signal(SIGINT, cleanup);
    signal(SIGHUP, cleanup);
    signal(SIGABRT, cleanup);

    strcpy(background, "white");
    strcpy(foreground, "black");
    

    tcgetattr(STDIN_FILENO, &original_term_attrs);
    
    raw_term_attrs = original_term_attrs;
    raw_term_attrs.c_lflag &= ~((uint)ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_term_attrs);

    read_term_response("\033[c", 'c', term_reply);
    
    {
        char *find_sixel_1 = strstr(term_reply, ";4;");
        char *find_sixel_2 = strstr(term_reply, "?4;");
        char *find_sixel_3 = strstr(term_reply, ";4c");
        
        if (find_sixel_1 != NULL) {
            has_sixel = true;
        } else if (find_sixel_2 != NULL) {
            has_sixel = true;
        } else if (find_sixel_3 != NULL) {
            has_sixel = true;
        }
    }

    if (has_sixel == 0) {
        if (force_sixel == NULL) {
            fprintf(stderr, "Error: Your terminal does not report having sixel graphics support.\n");
            cleanup(0);
        }
    }

    read_term_response("\033[?1;1;0S", 'S', term_reply);
    
    if (sscanf(term_reply, "\033[?1;0;%dS", &parsed_colors) == 1) {
        num_colors = parsed_colors;
    }

    if ((TERM = getenv("TERM")) == NULL) {
        error("TERM environment variable is not set.\n");
        fatal(EXIT_FAILURE);
    }

    if (!strncmp(TERM, "yaft", 4)) {
        num_colors = 256;
        strcpy(background, "black");
        strcpy(foreground, "white");
    }

    if (num_colors < 256) {
        read_term_response("\033[?1;3;256S", 'S', term_reply);
        if (sscanf(term_reply, "\033[?1;0;%dS", &parsed_colors) == 1) {
            num_colors = parsed_colors;
        }
    }

    read_term_response("\033]11;?\033\\", '\\', term_reply);
    
    read_term_response("\033[?2;1;0S", 'S', term_reply);
    
    {
        int32 parsed_width = 0;
        if (sscanf(term_reply, "\033[?2;1;%dS", &parsed_width) == 1) {
            if (parsed_width > 0) {
                screen_width = parsed_width;
            }
        } else {
            read_term_response("\033[14t", 't', term_reply);
            if (sscanf(term_reply, "\033[4;%d;%dt", &parsed_colors, &parsed_width) == 2) {
                if (parsed_width > 0) {
                    screen_width = parsed_width;
                }
            }
        }
    }

    if (strstr(TERM, "xterm")) {
        if (screen_width >= 1000) {
            screen_width = 1000;
        }
    }

    if (argc == 1) {
        DIR *directory;
        struct dirent *directory_entry;
        magic_t magic_cookie;
        int32 magic_load_result;

        if ((directory = opendir(".")) == NULL) {
            error("Error opening current directory: %s.\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
            
        if ((magic_cookie = magic_open(MAGIC_MIME_TYPE)) == NULL) {
            error("Error initializing magic library\n");
            fatal(EXIT_FAILURE);
        }
        
        magic_load_result = magic_load(magic_cookie, NULL);
        if (magic_load_result != 0) {
            error("Error loading magic database: %s\n", (char *)magic_error(magic_cookie));
            fatal(EXIT_FAILURE);
        }
        
        while ((directory_entry = readdir(directory))) {
            char *filename = directory_entry->d_name;
            int32 filename_len = strlen32(filename);
            const char *mime_type;
            
            if ((mime_type = magic_file(magic_cookie, filename)) == NULL) {
                continue;
            }

            if (BEGINS_WITH((char *)mime_type, "image/")) {
                list = realloc2(list, list_len, list_len + 1, SIZEOF(*list));
                list[list_len].path = malloc2(filename_len + strlen32(index0) + 1);
                strcpy(list[list_len].path, filename);
                list[list_len].len = filename_len;
                list_len += 1;
            }
        }
        
        magic_close(magic_cookie);
        closedir(directory);
        
        qsort64(list, list_len, SIZEOF(*list), compare_filenames);
    } else {
        for (int32 i = 1; i < argc; i += 1) {
            struct stat path_status;
            char *path = argv[i];
            int32 path_len = strlen32(path);

            if (stat(argv[i], &path_status) < 0) {
                error("Error in stat(%s): %s.\n", argv[i], strerror(errno));
                fatal(EXIT_FAILURE);
            }
            
            if (S_ISDIR(path_status.st_mode)) {
                continue;
            }

            list = realloc2(list, list_len, list_len + 1, SIZEOF(*list));
            list[list_len].path = malloc2(path_len + strlen32(index0) + 1);
            strcpy(list[list_len].path, path);
            list[list_len].len = path_len;
            list_len += 1;
        }
    }

    if (list_len <= 0) {
        cleanup(0);
    }

    char *montage_argv[10000];
    int32 montage_argc = 0;
    
    montage_argv[montage_argc++] = "magick";
    montage_argv[montage_argc++] = "montage";
    
    char tile_arg[64];
    SNPRINTF(tile_arg, "%dx1", num_tiles);
    montage_argv[montage_argc++] = "-tile";
    montage_argv[montage_argc++] = tile_arg;
    
    char geometry_arg[128];
    SNPRINTF(geometry_arg,
             "%dx%d>+%d+%d",
             tile_width, tile_height, tile_x_space, tile_y_space);
    montage_argv[montage_argc++] = "-geometry";
    montage_argv[montage_argc++] = geometry_arg;
    
    montage_argv[montage_argc++] = "-background";
    montage_argv[montage_argc++] = background;
    
    montage_argv[montage_argc++] = "-fill";
    montage_argv[montage_argc++] = foreground;
    
    montage_argv[montage_argc++] = "-auto-orient";
    
    if (num_colors > 16) {
        montage_argv[montage_argc++] = "-shadow";
    }
    
    if (strlen32(font_family) > 0) {
        montage_argv[montage_argc++] = "-font";
        montage_argv[montage_argc++] = font_family;
    }
    
    char font_size_string[64];
    if (font_size > 0) {
        SNPRINTF(font_size_string, "%d", font_size);
        montage_argv[montage_argc++] = "-pointsize";
        montage_argv[montage_argc++] = font_size_string;
    }

    int32 base_argc = montage_argc;
    int32 current_file_index = 0;
    
    while (current_file_index < list_len) {
        int32 goal;
        int32 remaining_files;
        int32 pipes[2];
        pid_t montage_pid;
        pid_t sixel_pid;

        montage_argc = base_argc;
        
        if ((goal = list_len - num_tiles) < 0) {
            goal = 0;
        }
        
        remaining_files = list_len - current_file_index;
        while (current_file_index < list_len && remaining_files > goal) {
            char *path = list[current_file_index].path;
            int32 path_len = list[current_file_index].len;
            
            if (ENDS_WITH(path, ".gif")) {
                memcpy64(path + path_len, index0, strlen32(index0) + 1);
            }
            
            if (ENDS_WITH(path, ".webp")) {
                memcpy64(path + path_len, index0, strlen32(index0) + 1);
            }
            
            montage_argv[montage_argc++] = "-label";
            montage_argv[montage_argc++] = path;
            montage_argv[montage_argc++] = path;
            
            current_file_index += 1;
            remaining_files = list_len - current_file_index;
        }
        
        montage_argv[montage_argc++] = "gif:-";
        montage_argv[montage_argc++] = NULL;
        
        xpipe(pipes);
        
        switch (montage_pid = fork()) {
        case -1:
            error("Error forking: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0: {
            XCLOSE(&pipes[0]);
            xdup2(pipes[1], STDOUT_FILENO);
            XCLOSE(&pipes[1]);
            
            xdup2(error_fd, STDERR_FILENO);
            
            execvp("magick", montage_argv);
            error("Error executing magick: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        default:
            break;
        }
        
        switch (sixel_pid = fork()) {
        case -1:
            error("Error forking: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0: {
            char num_colors_str[32];
            char *sixel_argv[6];
            
            XCLOSE(&pipes[1]);
            xdup2(pipes[0], STDIN_FILENO);
            XCLOSE(&pipes[0]);
            
            SNPRINTF(num_colors_str, "%d", num_colors);
            
            sixel_argv[0] = "magick";
            sixel_argv[1] = "-";
            sixel_argv[2] = "-colors";
            sixel_argv[3] = num_colors_str;
            sixel_argv[4] = "sixel:-";
            sixel_argv[5] = NULL;
            
            execvp("magick", sixel_argv);
            error("Error executing magick: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        default:
            break;
        }
        
        XCLOSE(&pipes[0]);
        XCLOSE(&pipes[1]);
        
        waitpid(montage_pid, NULL, 0);
        waitpid(sixel_pid, NULL, 0);
    }
    
    catfile(STDERR_FILENO, error_file);

    read_term_response("\033[c", 'c', term_reply);

    memory_check();
    cleanup(0);
}
