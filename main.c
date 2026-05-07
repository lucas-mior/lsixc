#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

typedef int int32;

struct termios original_terminal_attributes;

void cleanup(int signal_number) {
    tcsetattr(STDIN_FILENO, TCSANOW, &original_terminal_attributes);
    printf("\033\\");
    printf("\n");
    exit(0);
    return;
}

int32 read_terminal_response(char *sequence, char end_character, double timeout_seconds, char *output_buffer, int32 maximum_length) {
    int32 sequence_length = strlen(sequence);
    write(STDERR_FILENO, sequence, sequence_length);
    
    int32 index = 0;
    struct timeval timeout_structure;
    fd_set read_file_descriptors;
    
    while (1) {
        timeout_structure.tv_sec = (int32)timeout_seconds;
        timeout_structure.tv_usec = (int32)((timeout_seconds - (int32)timeout_seconds) * 1000000);
        
        FD_ZERO(&read_file_descriptors);
        FD_SET(STDIN_FILENO, &read_file_descriptors);
        
        int32 select_result = select(STDIN_FILENO + 1, &read_file_descriptors, NULL, NULL, &timeout_structure);
        if (select_result <= 0) {
            break;
        }
        
        char current_character;
        int32 read_bytes = read(STDIN_FILENO, &current_character, 1);
        if (read_bytes <= 0) {
            break;
        }
        
        if (index < maximum_length - 1) {
            output_buffer[index] = current_character;
            index += 1;
        }
        
        if (current_character == end_character) {
            break;
        }
    }
    
    output_buffer[index] = '\0';
    return index;
}

int main(int argc, char **argv) {
    signal(SIGINT, cleanup);
    signal(SIGHUP, cleanup);
    signal(SIGABRT, cleanup);

    int32 system_status = system("command -v magick > /dev/null 2>&1");
    if (system_status != 0) {
        fprintf(stderr, "Please install ImageMagick\n");
        return 1;
    }
    
    int32 num_colors = 16;
    char background[64];
    strcpy(background, "white");
    
    char foreground[64];
    strcpy(foreground, "black");
    
    int32 screen_width = 800;
    int32 tile_size = 120;
    int32 tile_width = tile_size;
    int32 tile_height = tile_size;
    int32 font_size = tile_width / 10;
    double timeout_seconds = 0.01;
    
    char font_family[64];
    strcpy(font_family, "Dejavu-Sans");

    tcgetattr(STDIN_FILENO, &original_terminal_attributes);
    
    struct termios raw_terminal_attributes;
    raw_terminal_attributes = original_terminal_attributes;
    raw_terminal_attributes.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_terminal_attributes);

    char terminal_reply[256];
    read_terminal_response("\033[c", 'c', 1.0, terminal_reply, sizeof(terminal_reply));
    
    int32 has_sixel = 0;
    char *force_sixel = getenv("LSIX_FORCE_SIXEL_SUPPORT");
    
    char *find_sixel_1 = strstr(terminal_reply, ";4;");
    char *find_sixel_2 = strstr(terminal_reply, "?4;");
    char *find_sixel_3 = strstr(terminal_reply, ";4c");
    
    if (find_sixel_1 != NULL) {
        has_sixel = 1;
    } else if (find_sixel_2 != NULL) {
        has_sixel = 1;
    } else if (find_sixel_3 != NULL) {
        has_sixel = 1;
    }

    if (has_sixel == 0) {
        if (force_sixel == NULL) {
            fprintf(stderr, "Error: Your terminal does not report having sixel graphics support.\n");
            cleanup(0);
        }
    }

    read_terminal_response("\033[?1;1;0S", 'S', timeout_seconds, terminal_reply, sizeof(terminal_reply));
    
    int32 parsed_colors = 0;
    int32 scan_result = sscanf(terminal_reply, "\033[?1;0;%dS", &parsed_colors);
    if (scan_result == 1) {
        num_colors = parsed_colors;
    }

    char *terminal_environment = getenv("TERM");
    if (terminal_environment != NULL) {
        int32 compare_yaft_result = strncmp(terminal_environment, "yaft", 4);
        if (compare_yaft_result == 0) {
            num_colors = 256;
        }
    }

    if (num_colors < 256) {
        read_terminal_response("\033[?1;3;256S", 'S', timeout_seconds, terminal_reply, sizeof(terminal_reply));
        scan_result = sscanf(terminal_reply, "\033[?1;0;%dS", &parsed_colors);
        if (scan_result == 1) {
            num_colors = parsed_colors;
        }
    }

    read_terminal_response("\033]11;?\033\\", '\\', timeout_seconds, terminal_reply, sizeof(terminal_reply));
    
    if (terminal_environment != NULL) {
        int32 compare_yaft_result = strncmp(terminal_environment, "yaft", 4);
        if (compare_yaft_result == 0) {
            strcpy(background, "black");
            strcpy(foreground, "white");
        }
    }

    read_terminal_response("\033[?2;1;0S", 'S', timeout_seconds, terminal_reply, sizeof(terminal_reply));
    
    int32 parsed_width = 0;
    scan_result = sscanf(terminal_reply, "\033[?2;1;%dS", &parsed_width);
    if (scan_result == 1) {
        if (parsed_width > 0) {
            screen_width = parsed_width;
        }
    } else {
        read_terminal_response("\033[14t", 't', timeout_seconds, terminal_reply, sizeof(terminal_reply));
        scan_result = sscanf(terminal_reply, "\033[4;%d;%dt", &parsed_colors, &parsed_width);
        if (scan_result == 2) {
            if (parsed_width > 0) {
                screen_width = parsed_width;
            }
        }
    }

    if (terminal_environment != NULL) {
        char *find_xterm = strstr(terminal_environment, "xterm");
        if (find_xterm != NULL) {
            if (screen_width >= 1000) {
                screen_width = 1000;
            }
        }
    }

    int32 tile_x_space = screen_width / 201;
    int32 tile_y_space = tile_x_space / 2;
    int32 width_denominator = tile_width + 2 * tile_x_space + 1;
    int32 num_tiles = screen_width / width_denominator;

    char temporary_error_file[256];
    int32 current_time = (int32)time(NULL);
    sprintf(temporary_error_file, "/tmp/lsix-%d.error", current_time);

    char **file_list = NULL;
    int32 file_count = 0;

    if (argc == 1) {
        DIR *directory_pointer = opendir(".");
        if (directory_pointer != NULL) {
            struct dirent *directory_entry;
            while (1) {
                directory_entry = readdir(directory_pointer);
                if (directory_entry == NULL) {
                    break;
                }
                
                char *filename = directory_entry->d_name;
                int32 filename_length = strlen(filename);
                
                if (filename_length > 4) {
                    char *extension_four_chars = &filename[filename_length - 4];
                    int32 is_image_file = 0;
                    
                    if (strcasecmp(extension_four_chars, ".jpg") == 0) {
                        is_image_file = 1;
                    } else if (strcasecmp(extension_four_chars, ".png") == 0) {
                        is_image_file = 1;
                    } else if (strcasecmp(extension_four_chars, ".gif") == 0) {
                        is_image_file = 1;
                    } else if (filename_length > 5) {
                        char *extension_five_chars = &filename[filename_length - 5];
                        if (strcasecmp(extension_five_chars, ".webp") == 0) {
                            is_image_file = 1;
                        } else if (strcasecmp(extension_five_chars, ".jpeg") == 0) {
                            is_image_file = 1;
                        }
                    }
                    
                    if (is_image_file == 1) {
                        file_list = realloc(file_list, (file_count + 1) * sizeof(char *));
                        file_list[file_count] = strdup(filename);
                        file_count += 1;
                    }
                }
            }
            closedir(directory_pointer);
            
            for (int32 i = 0; i < file_count - 1; i += 1) {
                int32 sorting_limit = file_count - i - 1;
                for (int32 j = 0; j < sorting_limit; j += 1) {
                    int32 compare_value = strcmp(file_list[j], file_list[j + 1]);
                    if (compare_value > 0) {
                        char *temporary_swap = file_list[j];
                        file_list[j] = file_list[j + 1];
                        file_list[j + 1] = temporary_swap;
                    }
                }
            }
        }
    } else {
        for (int32 i = 1; i < argc; i += 1) {
            struct stat path_status;
            stat(argv[i], &path_status);
            
            int32 is_directory = S_ISDIR(path_status.st_mode);
            if (is_directory) {
                printf("Recursing on %s\n", argv[i]);
                char recursion_command[1024];
                sprintf(recursion_command, "cd \"%s\" && %s", argv[i], argv[0]);
                system(recursion_command);
            } else {
                file_list = realloc(file_list, (file_count + 1) * sizeof(char *));
                file_list[file_count] = strdup(argv[i]);
                file_count += 1;
            }
        }
    }

    if (file_count == 0) {
        cleanup(0);
    }

    char image_magick_options[1024];
    sprintf(image_magick_options, "-tile %dx1 -geometry %dx%d>+%d+%d -background %s -fill %s -auto-orient ", 
            num_tiles, tile_width, tile_height, tile_x_space, tile_y_space, background, foreground);
    
    if (num_colors > 16) {
        strcat(image_magick_options, "-shadow ");
    }
    
    int32 family_length = strlen(font_family);
    if (family_length > 0) {
        strcat(image_magick_options, "-font ");
        strcat(image_magick_options, font_family);
        strcat(image_magick_options, " ");
    }
    
    if (font_size > 0) {
        char font_size_string[64];
        sprintf(font_size_string, "-pointsize %d ", font_size);
        strcat(image_magick_options, font_size_string);
    }

    int32 current_file_index = 0;
    
    while (current_file_index < file_count) {
        char montage_command[65536];
        strcpy(montage_command, "magick montage ");
        
        int32 goal = file_count - num_tiles;
        if (goal < 0) {
            goal = 0;
        }
        
        int32 remaining_files = file_count - current_file_index;
        while (current_file_index < file_count && remaining_files > goal) {
            char *current_file_name = file_list[current_file_index];
            
            char processed_label[1024];
            strcpy(processed_label, current_file_name);
            
            char *label_pointer = processed_label;
            if (label_pointer[0] == ':') {
                label_pointer += 1;
            }
            
            int32 label_length = strlen(label_pointer);
            for (int32 i = 0; i < label_length; i += 1) {
                int32 is_control_character = iscntrl((unsigned char)label_pointer[i]);
                if (is_control_character) {
                    label_pointer[i] = '?';
                }
            }
            
            char file_url[1024];
            strcpy(file_url, current_file_name);
            int32 name_length = strlen(current_file_name);
            
            if (name_length > 4) {
                char *extension_gif = &current_file_name[name_length - 4];
                if (strcasecmp(extension_gif, ".gif") == 0) {
                    strcat(file_url, "[0]");
                }
            }
            
            if (name_length > 5) {
                char *extension_webp = &current_file_name[name_length - 5];
                if (strcasecmp(extension_webp, ".webp") == 0) {
                    strcat(file_url, "[0]");
                }
            }
            
            strcat(montage_command, "-label \"");
            strcat(montage_command, label_pointer);
            strcat(montage_command, "\" \"file://");
            strcat(montage_command, file_url);
            strcat(montage_command, "\" ");
            
            current_file_index += 1;
            remaining_files = file_count - current_file_index;
        }
        
        char full_system_command[65536];
        sprintf(full_system_command, "%s %s gif:- 2>> %s | magick - -colors %d sixel:-", 
                montage_command, image_magick_options, temporary_error_file, num_colors);
        
        system(full_system_command);
    }
    
    char concatenate_error_command[512];
    sprintf(concatenate_error_command, "cat %s", temporary_error_file);
    system(concatenate_error_command);

    read_terminal_response("\033[c", 'c', 60.0, terminal_reply, sizeof(terminal_reply));

    cleanup(0);
    return 0;
}
