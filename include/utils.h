#ifndef UTILS_H
#define UTILS_H

#include "structures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 파일 입출력 함수
int load_user_data(const char* filename, UserInfo users[], int max_users);
int save_user_data(const char* filename, UserInfo users[], int user_count);

// 문자열 처리 함수
void trim_whitespace(char* str);
void to_lowercase(char* str);
void safe_strcpy(char* dest, const char* src, size_t dest_size);
int is_valid_string(const char* str);

// 보안 함수
void hash_password(const char* password, char* hash_output);
int verify_password(const char* password, const char* hash);
void generate_session_id(char* session_id);

// 로그 관리 함수
void write_log(const char* level, const char* message);
void write_error_log(const char* function, const char* error_message);
void write_access_log(const char* user_id, const char* action);

// 시간 관련 함수
char* get_current_time_string(void);
time_t get_current_timestamp(void);
int is_time_expired(time_t start_time, int timeout_seconds);

// 메모리 관리 함수
void* safe_malloc(size_t size);
void* safe_realloc(void* ptr, size_t size);
void safe_free(void** ptr);

// 화면 관리 함수 (클라이언트용)
void init_korean_console(void);
void clear_screen(void);
void print_header(const char* title);
void print_separator(void);
void wait_for_enter(void);

// 입력 유효성 검사 함수
int validate_user_id(const char* user_id);
int validate_password(const char* password);
int validate_menu_choice(int choice, int min, int max);

// 네트워크 메시지 처리 함수
void init_network_message(NetworkMessage* msg);
int serialize_message(NetworkMessage* msg, char* buffer, int buffer_size);
int deserialize_message(const char* buffer, NetworkMessage* msg);

// 데이터 검색 함수
int find_user_by_id(UserInfo users[], int user_count, const char* user_id);



#endif // UTILS_H 