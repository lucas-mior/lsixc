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

#include "util.c"
#define MEMORY_CHECK_USE_AFTER_FREE 0
#include "memory.c"

static struct termios original_term_attrs;
#define MAX_TERM_RESPONSE_LEN 256

static void __attribute((noreturn))
cleanup(int signal_number) {
    (void)signal_number;
    tcsetattr(STDIN_FILENO, TCSANOW, &original_term_attrs);
    printf("\033\\");
    exit(EXIT_SUCCESS);
}

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
        timeout.tv_usec = (int32)((timeout_seconds - (int32)timeout_seconds) * 1000000);
        
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
compare_strings(const void *a, const void *b) {
    char **string_a = (char **)a;
    char **string_b = (char **)b;
    
    return strcmp(*string_a, *string_b);
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

    int32 tile_x_space = screen_width / 201;
    int32 tile_y_space = tile_x_space / 2;
    int32 width_denominator = tile_width + 2 * tile_x_space + 1;
    int32 num_tiles = screen_width / width_denominator;

    char error_file[256];
    int32 current_time = (int32)time(NULL);
    SNPRINTF(error_file, "/tmp/lsix-%d.error", current_time);

    char **image_list = NULL;
    int32 image_list_len = 0;

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
                image_list = realloc2(image_list, image_list_len, image_list_len + 1, SIZEOF(char *));
                image_list[image_list_len] = malloc2(filename_len + 1);
                strcpy(image_list[image_list_len], filename);
                image_list_len += 1;
            }
        }
        
        magic_close(magic_cookie);
        closedir(directory);
        
        qsort64(image_list, image_list_len, SIZEOF(char *), compare_strings);
    } else {
        for (int32 i = 1; i < argc; i += 1) {
            struct stat path_status;
            stat(argv[i], &path_status);
            
            if (S_ISDIR(path_status.st_mode)) {
                continue;
            } else {
                int32 arg_len = strlen32(argv[i]);
                image_list = realloc2(image_list, image_list_len, image_list_len + 1, SIZEOF(char *));
                image_list[image_list_len] = malloc2(arg_len + 1);
                strcpy(image_list[image_list_len], argv[i]);
                image_list_len += 1;
            }
        }
    }

    if (image_list_len == 0) {
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
    SNPRINTF(geometry_arg, "%dx%d>+%d+%d", tile_width, tile_height, tile_x_space, tile_y_space);
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
    
    int32 family_len = strlen32(font_family);
    if (family_len > 0) {
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
    
    while (current_file_index < image_list_len) {
        montage_argc = base_argc;
        
        int32 goal = image_list_len - num_tiles;
        if (goal < 0) {
            goal = 0;
        }
        
        char **allocated_labels = malloc2(SIZEOF(char *) * image_list_len);
        char **allocated_urls = malloc2(SIZEOF(char *) * image_list_len);
        int32 alloc_count = 0;
        
        int32 remaining_files = image_list_len - current_file_index;
        while (current_file_index < image_list_len && remaining_files > goal) {
            char *current_file_name = image_list[current_file_index];
            char *label_pointer;
            int32 label_len;
            char *file_url;
            int32 name_len;
            
            char *processed_label = malloc2(strlen32(current_file_name) + 1);
            strcpy(processed_label, current_file_name);
            allocated_labels[alloc_count] = processed_label;
            
            label_pointer = processed_label;
            if (label_pointer[0] == ':') {
                label_pointer += 1;
            }
            
            label_len = strlen32(label_pointer);
            for (int32 i = 0; i < label_len; i += 1) {
                if (iscntrl((unsigned char)label_pointer[i])) {
                    label_pointer[i] = '?';
                }
            }
            
            file_url = malloc2(1024);
            allocated_urls[alloc_count] = file_url;
            
            strcpy(file_url, "file://");
            strcat(file_url, current_file_name);
            
            name_len = strlen32(current_file_name);
            if (name_len > 4) {
                char *extension_gif = &current_file_name[name_len - 4];
                if (strcasecmp(extension_gif, ".gif") == 0) {
                    strcat(file_url, "[0]");
                }
            }
            
            if (name_len > 5) {
                char *extension_webp = &current_file_name[name_len - 5];
                if (strcasecmp(extension_webp, ".webp") == 0) {
                    strcat(file_url, "[0]");
                }
            }
            
            montage_argv[montage_argc++] = "-label";
            montage_argv[montage_argc++] = label_pointer;
            montage_argv[montage_argc++] = file_url;
            
            alloc_count += 1;
            current_file_index += 1;
            remaining_files = image_list_len - current_file_index;
        }
        
        montage_argv[montage_argc++] = "gif:-";
        montage_argv[montage_argc++] = NULL;
        
        int32 pipe_descriptors[2];
        pipe(pipe_descriptors);
        
        pid_t montage_pid;
        switch (montage_pid = fork()) {
        case -1:
            error("Error forking: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0: {
            int32 error_fd;
            
            XCLOSE(&pipe_descriptors[0]);
            xdup2(pipe_descriptors[1], STDOUT_FILENO);
            XCLOSE(&pipe_descriptors[1]);
            
            error_fd = open(error_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (error_fd != -1) {
                xdup2(error_fd, STDERR_FILENO);
                XCLOSE(&error_fd);
            }
            
            execvp("magick", montage_argv);
            error("Error executing magick: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        }
        default:
            break;
        }
        
        pid_t sixel_pid;
        switch (sixel_pid = fork()) {
        case -1:
            error("Error forking: %s\n", strerror(errno));
            fatal(EXIT_FAILURE);
        case 0: {
            char num_colors_str[32];
            char *sixel_argv[6];
            
            XCLOSE(&pipe_descriptors[1]);
            xdup2(pipe_descriptors[0], STDIN_FILENO);
            XCLOSE(&pipe_descriptors[0]);
            
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
        
        XCLOSE(&pipe_descriptors[0]);
        XCLOSE(&pipe_descriptors[1]);
        
        waitpid(montage_pid, NULL, 0);
        waitpid(sixel_pid, NULL, 0);
        
        for (int32 i = 0; i < alloc_count; i += 1) {
            free2(allocated_labels[i], strlen32(allocated_labels[i]) + 1);
            free2(allocated_urls[i], 1024);
        }
        free2(allocated_labels, SIZEOF(char *) * image_list_len);
        free2(allocated_urls, SIZEOF(char *) * image_list_len);
    }
    
    {
        int fd;
        char buffer[4096];
        int64 r;

        if ((fd = open(error_file, O_RDONLY)) < 0) {
            error("Error opening %s: %s.\n", error_file, strerror(errno));
            fatal(EXIT_FAILURE);
        }

        printf("\n");
        while ((r = read64(fd, buffer, SIZEOF(buffer))) > 0) {
            write_all(STDERR_FILENO, buffer, r);
        }
        if (r < 0) {
            error("Error reading %s: %s.\n", error_file, strerror(errno));
        }
    }

    read_term_response("\033[c", 'c', term_reply);

    memory_check();
    cleanup(0);
}
