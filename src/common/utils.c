#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

// UTF-8 ������ ���� �ʱ�ȭ �Լ�
void init_korean_console(void) {
#ifdef _WIN32
    // �ܼ� �ڵ��������� UTF-8(CP65001)�� ����
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    
    // �ܼ� ��� ����
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
}

// ���ڿ� ó�� �Լ�
void trim_whitespace(char* str) {
    if (!str) return;
    
    // ���� ���� ����
    char* start = str;
    while (isspace((unsigned char)*start)) start++;
    
    // ���� ���� ����
    char* end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    // ��� ����
    size_t len = end - start + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

void to_lowercase(char* str) {
    if (!str) return;
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

void safe_strcpy(char* dest, const char* src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

int is_valid_string(const char* str) {
    return str != NULL && strlen(str) > 0;
}

// ���� �Լ� (������ ����)
void hash_password(const char* password, char* hash_output) {
    if (!password || !hash_output) return;
    
    // ������ �ؽ� �Լ� (�����δ� SHA-256 ����ؾ� ��)
    unsigned long hash = 5381;
    const char* str = password;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    sprintf(hash_output, "%08lx", hash);
}

int verify_password(const char* password, const char* hash) {
    if (!password || !hash) return 0;
    
    char computed_hash[MAX_STRING_LEN];
    hash_password(password, computed_hash);
    
    return strcmp(computed_hash, hash) == 0;
}

void generate_session_id(char* session_id) {
    if (!session_id) return;
    
    // ������ ���� ID ����
    srand((unsigned int)time(NULL));
    sprintf(session_id, "sess_%08x_%08x", rand(), (unsigned int)time(NULL));
}

// �α� ���� �Լ�
void write_log(const char* level, const char* message) {
    if (!level || !message) return;
    
    time_t now;
    time(&now);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; // ���� ���� ����
    
    printf("[%s] [%s] %s\n", time_str, level, message);
    fflush(stdout);
}

void write_error_log(const char* function, const char* error_message) {
    if (!function || !error_message) return;
    
    char log_msg[MAX_CONTENT_LEN];
    snprintf(log_msg, sizeof(log_msg), "%s: %s", function, error_message);
    write_log("ERROR", log_msg);
}

void write_access_log(const char* user_id, const char* action) {
    if (!user_id || !action) return;
    
    char log_msg[MAX_CONTENT_LEN];
    snprintf(log_msg, sizeof(log_msg), "User[%s] %s", user_id, action);
    write_log("ACCESS", log_msg);
}

// �ð� ���� �Լ�
char* get_current_time_string(void) {
    static char time_str[MAX_STRING_LEN];
    time_t now;
    time(&now);
    
    strcpy(time_str, ctime(&now));
    time_str[strlen(time_str) - 1] = '\0'; // ���� ���� ����
    
    return time_str;
}

time_t get_current_timestamp(void) {
    return time(NULL);
}

int is_time_expired(time_t start_time, int timeout_seconds) {
    return (time(NULL) - start_time) > timeout_seconds;
}

// �޸� ���� �Լ�
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr && size > 0) {
        write_error_log("safe_malloc", "Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        write_error_log("safe_realloc", "Memory reallocation failed");
        free(ptr);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

void safe_free(void** ptr) {
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
    }
}

// ȭ�� ���� �Լ�
void clear_screen(void) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void print_header(const char* title) {
    if (!title) return;
    
    printf("\n");
    printf("================================================\n");
    printf("  %s\n", title);
    printf("================================================\n");
}

void print_separator(void) {
    printf("------------------------------------------------\n");
}

void wait_for_enter(void) {
    printf("\n����Ϸ��� Enter�� �����ּ���...");
    fflush(stdout);
    
    // �Է� ���� ����
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// �Է� ��ȿ�� �˻� �Լ�
int validate_user_id(const char* user_id) {
    if (!user_id) return 0;
    
    int len = strlen(user_id);
    if (len < 3 || len > 20) return 0;
    
    // ������, ���ڸ� ���
    for (int i = 0; i < len; i++) {
        if (!isalnum((unsigned char)user_id[i])) return 0;
    }
    
    return 1;
}

int validate_password(const char* password) {
    if (!password) return 0;
    
    int len = strlen(password);
    return len >= 4 && len <= 20;
}

int validate_menu_choice(int choice, int min, int max) {
    return choice >= min && choice <= max;
}

// ��Ʈ��ũ �޽��� ó�� �Լ�
void init_network_message(NetworkMessage* msg) {
    if (!msg) return;
    
    memset(msg, 0, sizeof(NetworkMessage));
}

int serialize_message(NetworkMessage* msg, char* buffer, int buffer_size) {
    if (!msg || !buffer || buffer_size <= 0) return 0;
    
    // ������ ����ȭ (�����δ� JSON ���)
    int written = snprintf(buffer, buffer_size,
        "%d|%s|%s|%d|%s",
        msg->message_type,
        msg->user_id,
        msg->session_id,
        msg->data_length,
        msg->data
    );
    
    return written > 0 && written < buffer_size;
}

int deserialize_message(const char* buffer, NetworkMessage* msg) {
    if (!buffer || !msg) return 0;
    
    // ������ ������ȭ
    return sscanf(buffer, "%d|%255[^|]|%255[^|]|%d|%2047[^\n]",
        &msg->message_type,
        msg->user_id,
        msg->session_id,
        &msg->data_length,
        msg->data
    ) == 5;
}

// ���� ����� �Լ� (�⺻ ����)
int load_user_data(const char* filename, UserInfo users[], int max_users) {
    if (!filename || !users) return 0;
    
    FILE* file = fopen(filename, "r");
    if (!file) return 0;
    
    int count = 0;
    char line[MAX_STRING_LEN * 2];
    
    while (fgets(line, sizeof(line), file) && count < max_users) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            safe_strcpy(users[count].user_id, line, sizeof(users[count].user_id));
            safe_strcpy(users[count].password_hash, colon + 1, sizeof(users[count].password_hash));
            
            // ���� ���� ����
            trim_whitespace(users[count].password_hash);
            
            users[count].login_attempts = 0;
            users[count].is_locked = 0;
            users[count].is_online = 0;
            
            count++;
        }
    }
    
    fclose(file);
    return count;
}

int save_user_data(const char* filename, UserInfo users[], int user_count) {
    if (!filename || !users) return 0;
    
    FILE* file = fopen(filename, "w");
    if (!file) return 0;
    
    for (int i = 0; i < user_count; i++) {
        fprintf(file, "%s:%s\n", users[i].user_id, users[i].password_hash);
    }
    
    fclose(file);
    return 1;
} 