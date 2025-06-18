#include "server.h"
#include "utils.h"
#include "api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <direct.h>
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

// 전역 서버 데이터
static ServerData g_server_data;
static int g_server_running = 0;

// 함수 선언
void handle_client_simple(socket_t client_socket);

// 데이터 파일 경로
#define ELECTIONS_FILE "data/elections.txt"
#define CANDIDATES_FILE "data/candidates.txt"
#define PLEDGES_FILE "data/pledges.txt"
#define UPDATE_TIME_FILE "data/last_update.txt"

// 신호 처리
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        write_log("INFO", "Server shutdown signal received");
        g_server_running = 0;
    }
}

// 서버 초기화
int init_server(void) {
    write_log("INFO", "Initializing server...");
    
    // 데이터 구조체 초기화
    memset(&g_server_data, 0, sizeof(ServerData));
    
    // 뮤텍스 초기화
#ifdef _WIN32
    InitializeCriticalSection(&g_server_data.data_mutex);
    InitializeCriticalSection(&g_server_data.client_mutex);
#else
    if (pthread_mutex_init(&g_server_data.data_mutex, NULL) != 0) {
        write_error_log("init_server", "Failed to initialize data mutex");
        return 0;
    }
    
    if (pthread_mutex_init(&g_server_data.client_mutex, NULL) != 0) {
        write_error_log("init_server", "Failed to initialize client mutex");
        pthread_mutex_destroy(&g_server_data.data_mutex);
        return 0;
    }
#endif
    
    // Windows 소켓 초기화
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        write_error_log("init_server", "WSAStartup failed");
        return 0;
    }
#endif
    
    // data 디렉토리 생성 확인
    printf("📁 데이터 디렉토리 확인 중...\n");
    fflush(stdout);
#ifdef _WIN32
    _mkdir("data");
#else
    mkdir("data", 0755);
#endif
    
    // 사용자 데이터 로드
    printf("👤 사용자 데이터 로드 중...\n");
    fflush(stdout);
    g_server_data.user_count = load_user_data("data/users.txt", 
        g_server_data.users, MAX_USERS);
    
    if (g_server_data.user_count == 0) {
        write_log("WARNING", "No user data loaded, creating default admin user");
        printf("⚙️  기본 관리자 계정 생성 중...\n");
        
        // 기본 관리자 계정 생성
        strcpy(g_server_data.users[0].user_id, "admin");
        hash_password("admin", g_server_data.users[0].password_hash);
        g_server_data.users[0].login_attempts = 0;
        g_server_data.users[0].is_locked = 0;
        g_server_data.users[0].is_online = 0;
        g_server_data.user_count = 1;
        
        // 파일에 저장
        save_user_data("data/users.txt", g_server_data.users, g_server_data.user_count);
        printf("✅ 기본 관리자 계정(admin/admin) 생성 완료\n");
    } else {
        printf("✅ 사용자 데이터 %d개 로드 완료\n", g_server_data.user_count);
    }
    
    // 기존 데이터 로드
    printf("📊 기존 데이터 로드 중...\n");
    g_server_data.election_count = load_elections_from_file(g_server_data.elections, MAX_ELECTIONS);
    printf("   선거 정보: %d개\n", g_server_data.election_count);
    
    g_server_data.candidate_count = load_candidates_from_file(g_server_data.candidates, MAX_CANDIDATES);
    printf("   후보자 정보: %d개\n", g_server_data.candidate_count);
    
    g_server_data.pledge_count = load_pledges_from_file(g_server_data.pledges, MAX_PLEDGES);
    printf("   공약 정보: %d개\n", g_server_data.pledge_count);
    
    // 평가 데이터 로드
    printf("📈 평가 데이터 로드 중...\n");
    int eval_count = load_evaluations_from_file();
    printf("   평가 데이터: %d개\n", eval_count);
    
    // 모든 공약의 통계 업데이트
    printf("🔄 공약 통계 초기화 중...\n");
    for (int i = 0; i < g_server_data.pledge_count; i++) {
        update_pledge_statistics(g_server_data.pledges[i].pledge_id);
    }
    printf("✅ 공약 통계 초기화 완료\n");
    
    write_log("INFO", "Server initialized successfully");
    return 1;
}

// 클라이언트 처리 스레드 구조체
typedef struct {
    socket_t client_socket;
    int client_id;
} ClientThreadData;

#ifdef _WIN32
DWORD WINAPI handle_client_thread(LPVOID param) {
    ClientThreadData* data = (ClientThreadData*)param;
    printf("🧵 스레드 시작: 클라이언트 %d\n", data->client_id);
    
    handle_client_simple(data->client_socket);
    
    printf("🧵 스레드 종료: 클라이언트 %d\n", data->client_id);
    // 클라이언트 소켓 정리
    closesocket(data->client_socket);
    free(data);
    return 0;
}
#else
void* handle_client_thread(void* param) {
    ClientThreadData* data = (ClientThreadData*)param;
    printf("🧵 스레드 시작: 클라이언트 %d\n", data->client_id);
    
    handle_client_simple(data->client_socket);
    
    printf("🧵 스레드 종료: 클라이언트 %d\n", data->client_id);
    // 클라이언트 소켓 정리
    close(data->client_socket);
    free(data);
    return NULL;
}
#endif

// NetworkMessage 기반 클라이언트 처리
void handle_client_simple(socket_t client_socket) {
    NetworkMessage request, response;
    int bytes_received;
    
    write_log("INFO", "Client connected");
    printf("✅ 클라이언트가 연결되었습니다!\n");
    
    while (g_server_running) {
        // NetworkMessage 구조체로 요청 수신
        memset(&request, 0, sizeof(NetworkMessage));
        bytes_received = recv(client_socket, (char*)&request, sizeof(NetworkMessage), 0);
        
        if (bytes_received <= 0) {
            printf("📤 클라이언트 연결이 종료되었습니다.\n");
            break;
        }
        
        if (bytes_received != sizeof(NetworkMessage)) {
            printf("⚠️  잘못된 메시지 크기: %d bytes (예상: %zu bytes)\n", 
                   bytes_received, sizeof(NetworkMessage));
            continue;
        }
        
        printf("📨 메시지 수신: 타입=%d, 사용자=%s\n", 
               request.message_type, request.user_id);
        
        // 응답 메시지 초기화
        memset(&response, 0, sizeof(NetworkMessage));
        
        // 메시지 타입에 따른 처리
        switch (request.message_type) {
            case MSG_LOGIN_REQUEST:
                handle_login_request(&request, &response);
                break;
                
            case MSG_LOGOUT_REQUEST:
                handle_logout_request(&request, &response);
                break;
                
            case MSG_GET_ELECTIONS:
                handle_get_elections_request(&response);
                break;
                
            case MSG_GET_CANDIDATES:
                // 새로고침 명령 확인
                if (strcmp(request.data, "refresh_candidates") == 0) {
                    printf("🔄 후보자 정보 새로고침 요청 수신\n");
                    
                    // API 데이터 수집 실행
                    response.message_type = MSG_SUCCESS;
                    printf("🔄 API 데이터 수집을 시작합니다...\n");
                    
                    if (collect_api_data()) {
                        response.status_code = STATUS_SUCCESS;
                        strcpy(response.data, "후보자 정보 새로고침 완료");
                        response.data_length = strlen(response.data);
                        printf("✅ 후보자 정보 새로고침 성공\n");
                    } else {
                        response.status_code = STATUS_INTERNAL_ERROR;
                        strcpy(response.data, "후보자 정보 새로고침 실패");
                        response.data_length = strlen(response.data);
                        printf("❌ 후보자 정보 새로고침 실패\n");
                    }
                } else {
                    // 일반적인 후보자 조회 요청
                    handle_get_candidates_request(request.data, &response);
                }
                break;
                
            case MSG_GET_PLEDGES:
                // request.data에서 candidate_id 추출 필요
                handle_get_pledges_request("", &response);
                break;
                
            case MSG_REFRESH_ELECTIONS:
                printf("🔄 선거 정보 새로고침 요청 수신\n");
                response.message_type = MSG_SUCCESS;
                
                if (collect_elections_only()) {
                    response.status_code = STATUS_SUCCESS;
                    strcpy(response.data, "선거 정보 새로고침 완료");
                    response.data_length = strlen(response.data);
                    printf("✅ 선거 정보 새로고침 성공\n");
                } else {
                response.status_code = STATUS_INTERNAL_ERROR;
                    strcpy(response.data, "선거 정보 새로고침 실패");
                    response.data_length = strlen(response.data);
                    printf("❌ 선거 정보 새로고침 실패\n");
                }
                break;
                
            case MSG_REFRESH_CANDIDATES:
                printf("🔄 후보자 정보 새로고침 요청 수신\n");
                response.message_type = MSG_SUCCESS;
                
                if (collect_candidates_only()) {
                response.status_code = STATUS_SUCCESS;
                    strcpy(response.data, "후보자 정보 새로고침 완료");
                    response.data_length = strlen(response.data);
                    printf("✅ 후보자 정보 새로고침 성공\n");
                } else {
                    response.status_code = STATUS_INTERNAL_ERROR;
                    strcpy(response.data, "후보자 정보 새로고침 실패");
                    response.data_length = strlen(response.data);
                    printf("❌ 후보자 정보 새로고침 실패\n");
                }
                break;
                
            case MSG_REFRESH_PLEDGES:
                printf("🔄 공약 정보 새로고침 요청 수신\n");
                response.message_type = MSG_SUCCESS;
                
                if (collect_pledges_only()) {
                    response.status_code = STATUS_SUCCESS;
                    strcpy(response.data, "공약 정보 새로고침 완료");
                    response.data_length = strlen(response.data);
                    printf("✅ 공약 정보 새로고침 성공\n");
                } else {
                    response.status_code = STATUS_INTERNAL_ERROR;
                    strcpy(response.data, "공약 정보 새로고침 실패");
                    response.data_length = strlen(response.data);
                    printf("❌ 공약 정보 새로고침 실패\n");
                }
                break;
                
            case MSG_REFRESH_ALL:
                printf("🔄 전체 데이터 새로고침 요청 수신\n");
                response.message_type = MSG_SUCCESS;
                
                if (collect_api_data()) {
                    response.status_code = STATUS_SUCCESS;
                    strcpy(response.data, "전체 데이터 새로고침 완료");
                    response.data_length = strlen(response.data);
                    printf("✅ 전체 데이터 새로고침 성공\n");
                } else {
                    response.status_code = STATUS_INTERNAL_ERROR;
                    strcpy(response.data, "전체 데이터 새로고침 실패");
                    response.data_length = strlen(response.data);
                    printf("❌ 전체 데이터 새로고침 실패\n");
                }
                break;
                
            case MSG_EVALUATE_PLEDGE:
                {
                    // 평가 요청 처리
                    // data 형식: "pledge_id|evaluation_type" (예: "100120965_1|1")
                    char pledge_id[MAX_STRING_LEN];
                    int evaluation_type = 0;
                    
                    if (sscanf(request.data, "%255[^|]|%d", pledge_id, &evaluation_type) == 2) {
                        handle_evaluate_pledge_request(request.user_id, pledge_id, evaluation_type, &response);
                    } else {
                        response.message_type = MSG_ERROR;
                        response.status_code = STATUS_BAD_REQUEST;
                        strcpy(response.data, "평가 데이터 형식이 올바르지 않습니다 (형식: pledge_id|evaluation_type)");
                    }
                }
                break;
                
            case MSG_CANCEL_EVALUATION:
                {
                    // 평가 취소 요청 처리
                    // data 형식: "pledge_id" (예: "100120965_1")
                    char pledge_id[MAX_STRING_LEN];
                    
                    if (sscanf(request.data, "%255s", pledge_id) == 1) {
                        handle_cancel_evaluation_request(request.user_id, pledge_id, &response);
                    } else {
                        response.message_type = MSG_ERROR;
                        response.status_code = STATUS_BAD_REQUEST;
                        strcpy(response.data, "공약 ID가 올바르지 않습니다");
                    }
                }
                break;
                
            case MSG_GET_USER_EVALUATION:
                {
                    // 사용자 평가 조회 요청 처리
                    // data 형식: "pledge_id" (예: "100120965_1")
                    char pledge_id[MAX_STRING_LEN];
                    
                    if (sscanf(request.data, "%255s", pledge_id) == 1) {
                        handle_get_user_evaluation_request(request.user_id, pledge_id, &response);
                    } else {
                        response.message_type = MSG_ERROR;
                        response.status_code = STATUS_BAD_REQUEST;
                        strcpy(response.data, "공약 ID가 올바르지 않습니다");
                    }
                }
                break;
                
            case MSG_GET_STATISTICS:
                {
                    // 통계 요청 처리
                    // data 형식: "pledge_id" (예: "100120965_1")
                    char pledge_id[MAX_STRING_LEN];
                    
                    if (sscanf(request.data, "%255s", pledge_id) == 1) {
                        handle_get_statistics_request(pledge_id, &response);
                    } else {
                        response.message_type = MSG_ERROR;
                        response.status_code = STATUS_BAD_REQUEST;
                        strcpy(response.data, "공약 ID가 올바르지 않습니다");
                    }
                }
                break;
                
            default:
                printf("❌ 알 수 없는 메시지 타입: %d\n", request.message_type);
                response.message_type = MSG_ERROR;
                response.status_code = STATUS_BAD_REQUEST;
                strcpy(response.data, "지원하지 않는 메시지 타입입니다");
                break;
        }
        
        // 응답 전송
        int bytes_sent = send(client_socket, (char*)&response, sizeof(NetworkMessage), 0);
        if (bytes_sent <= 0) {
            printf("❌ 응답 전송 실패\n");
            break;
        }
        
        printf("📤 응답 전송: 타입=%d, 상태=%d\n", 
               response.message_type, response.status_code);
    }
    
#ifdef _WIN32
    closesocket(client_socket);
#else
    close(client_socket);
#endif
    
    write_log("INFO", "Client disconnected");
}

// 로그인 요청 처리
void handle_login_request(NetworkMessage* request, NetworkMessage* response) {
    printf("🔐 로그인 요청 처리 중...\n");
    
    // JSON 파싱 (간단한 구현)
    char user_id[MAX_STRING_LEN] = "";
    char password[MAX_STRING_LEN] = "";
    char request_type[32] = "login";  // 기본값은 로그인
    
    // request.data에서 사용자 정보 추출
    if (parse_login_json(request->data, user_id, password, request_type)) {
        printf("   👤 사용자: %s, 요청타입: %s\n", user_id, request_type);
        
        // 회원가입 요청인 경우
        if (strcmp(request_type, "register") == 0) {
            handle_register_request(user_id, password, response);
            return;
        }
        
        // 로그인 요청 처리
        if (authenticate_user_server(user_id, password)) {
            // 로그인 성공
            char session_id[MAX_STRING_LEN];
            generate_session_id_server(session_id, user_id);
            
            response->message_type = MSG_LOGIN_RESPONSE;
            response->status_code = STATUS_SUCCESS;
            strcpy(response->user_id, user_id);
            strcpy(response->session_id, session_id);
            strcpy(response->data, "로그인 성공");
            response->data_length = strlen(response->data);
            
            printf("✅ 로그인 성공: %s (세션: %.8s...)\n", user_id, session_id);
        } else {
            // 로그인 실패
            response->message_type = MSG_LOGIN_RESPONSE;
            response->status_code = STATUS_UNAUTHORIZED;
            strcpy(response->data, "아이디 또는 비밀번호가 올바르지 않습니다");
            response->data_length = strlen(response->data);
            
            printf("❌ 로그인 실패: %s\n", user_id);
        }
    } else {
        // JSON 파싱 실패
        response->message_type = MSG_LOGIN_RESPONSE;
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "잘못된 로그인 데이터 형식입니다");
        response->data_length = strlen(response->data);
        
        printf("❌ JSON 파싱 실패: %s\n", request->data);
    }
}

// 회원가입 요청 처리
void handle_register_request(const char* user_id, const char* password, NetworkMessage* response) {
    printf("📝 회원가입 요청 처리 중: %s\n", user_id);
    
    // 중복 사용자 확인
    if (find_user_by_id_server(user_id) != NULL) {
        response->message_type = MSG_LOGIN_RESPONSE;
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "이미 존재하는 사용자 ID입니다");
        response->data_length = strlen(response->data);
        printf("❌ 회원가입 실패: 중복된 ID\n");
        return;
    }
    
    // 새 사용자 추가
    if (add_new_user_to_server(user_id, password)) {
        response->message_type = MSG_LOGIN_RESPONSE;
        response->status_code = STATUS_SUCCESS;
        strcpy(response->data, "회원가입 성공");
        response->data_length = strlen(response->data);
        printf("✅ 회원가입 성공: %s\n", user_id);
    } else {
        response->message_type = MSG_LOGIN_RESPONSE;
        response->status_code = STATUS_INTERNAL_ERROR;
        strcpy(response->data, "회원가입 처리 중 오류가 발생했습니다");
        response->data_length = strlen(response->data);
        printf("❌ 회원가입 실패: 서버 오류\n");
    }
}

// 로그아웃 요청 처리
void handle_logout_request(NetworkMessage* request, NetworkMessage* response) {
    printf("🚪 로그아웃 요청: %s\n", request->user_id);
    
    response->message_type = MSG_SUCCESS;
    response->status_code = STATUS_SUCCESS;
    strcpy(response->data, "로그아웃 완료");
    response->data_length = strlen(response->data);
    
    printf("✅ 로그아웃 완료: %s\n", request->user_id);
}

// 선거 정보 요청 처리  
void handle_get_elections_request(NetworkMessage* response) {
    printf("📊 선거 정보 요청 처리\n");
    
    response->message_type = MSG_SUCCESS;
    response->status_code = STATUS_SUCCESS;
    snprintf(response->data, sizeof(response->data), 
             "선거 정보 %d개 조회 가능", g_server_data.election_count);
    response->data_length = strlen(response->data);
}

// 후보자 정보 요청 처리
void handle_get_candidates_request(const char* election_id, NetworkMessage* response) {
    printf("👥 후보자 정보 요청 처리\n");
    
    response->message_type = MSG_SUCCESS;
    response->status_code = STATUS_SUCCESS;
    snprintf(response->data, sizeof(response->data), 
             "후보자 정보 %d개 조회 가능", g_server_data.candidate_count);
    response->data_length = strlen(response->data);
}

// 공약 정보 요청 처리
void handle_get_pledges_request(const char* candidate_id, NetworkMessage* response) {
    printf("📋 공약 정보 요청 처리\n");
    
    response->message_type = MSG_SUCCESS;
    response->status_code = STATUS_SUCCESS;
    snprintf(response->data, sizeof(response->data), 
             "공약 정보 %d개 조회 가능", g_server_data.pledge_count);
    response->data_length = strlen(response->data);
}

// JSON 파싱 함수 (간단한 구현)
int parse_login_json(const char* json_data, char* user_id, char* password, char* request_type) {
    if (!json_data || !user_id || !password) return 0;
    
    // 간단한 JSON 파싱 (정규식 대신 문자열 검색 사용)
    char* type_start = strstr(json_data, "\"type\":\"");
    if (type_start) {
        type_start += 8; // "type":" 길이
        char* type_end = strchr(type_start, '"');
        if (type_end) {
            int len = type_end - type_start;
            if (len > 0 && len < 31) {
                strncpy(request_type, type_start, len);
                request_type[len] = '\0';
            }
        }
    }
    
    char* userid_start = strstr(json_data, "\"user_id\":\"");
    if (!userid_start) return 0;
    userid_start += 11; // "user_id":" 길이
    
    char* userid_end = strchr(userid_start, '"');
    if (!userid_end) return 0;
    
    int userid_len = userid_end - userid_start;
    if (userid_len <= 0 || userid_len >= MAX_STRING_LEN) return 0;
    
    strncpy(user_id, userid_start, userid_len);
    user_id[userid_len] = '\0';
    
    char* password_start = strstr(json_data, "\"password\":\"");
    if (!password_start) return 0;
    password_start += 12; // "password":" 길이
    
    char* password_end = strchr(password_start, '"');
    if (!password_end) return 0;
    
    int password_len = password_end - password_start;
    if (password_len <= 0 || password_len >= MAX_STRING_LEN) return 0;
    
    strncpy(password, password_start, password_len);
    password[password_len] = '\0';
    
    return 1;
}

// 서버 사용자 인증
int authenticate_user_server(const char* user_id, const char* password) {
    for (int i = 0; i < g_server_data.user_count; i++) {
        if (strcmp(g_server_data.users[i].user_id, user_id) == 0) {
            return verify_password(password, g_server_data.users[i].password_hash);
        }
    }
    return 0;
}

// 사용자 ID로 사용자 찾기
UserInfo* find_user_by_id_server(const char* user_id) {
    for (int i = 0; i < g_server_data.user_count; i++) {
        if (strcmp(g_server_data.users[i].user_id, user_id) == 0) {
            return &g_server_data.users[i];
        }
    }
    return NULL;
}

// 서버에 새 사용자 추가
int add_new_user_to_server(const char* user_id, const char* password) {
    if (g_server_data.user_count >= MAX_USERS) {
        return 0;
    }
    
    UserInfo* new_user = &g_server_data.users[g_server_data.user_count];
    memset(new_user, 0, sizeof(UserInfo));
    
    strcpy(new_user->user_id, user_id);
    hash_password(password, new_user->password_hash);
    new_user->login_attempts = 0;
    new_user->is_locked = 0;
    new_user->is_online = 0;
    new_user->last_login = 0;
    
    g_server_data.user_count++;
    
    // 파일에 저장
    return save_user_data("data/users.txt", g_server_data.users, g_server_data.user_count);
}

// 세션 ID 생성
void generate_session_id_server(char* session_id, const char* user_id) {
    time_t now = time(NULL);
    int random_num = rand() % 10000;
    
    snprintf(session_id, MAX_STRING_LEN, "sess_%s_%lld_%d", 
             user_id, (long long)now, random_num);
}

// 서버 시작
int start_server(int port) {
    socket_t server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    write_log("INFO", "Starting server...");
    
    // 서버 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        write_error_log("start_server", "Failed to create socket");
        return 0;
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // 소켓 바인딩
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        write_error_log("start_server", "Failed to bind socket");
#ifdef _WIN32
        closesocket(server_socket);
#else
        close(server_socket);
#endif
        return 0;
    }
    
    // 연결 대기
    if (listen(server_socket, MAX_CLIENTS) == SOCKET_ERROR) {
        write_error_log("start_server", "Failed to listen on socket");
#ifdef _WIN32
        closesocket(server_socket);
#else
        close(server_socket);
#endif
        return 0;
    }
    
    char log_msg[MAX_STRING_LEN];
    snprintf(log_msg, sizeof(log_msg), "Server listening on port %d", port);
    write_log("INFO", log_msg);
    
    g_server_running = 1;
    int client_counter = 0;
    
    // 클라이언트 연결 처리 (다중 클라이언트 지원)
    printf("🚀 다중 클라이언트 서버 시작 (최대 %d개 동시 연결 지원)\n", MAX_CLIENTS);
    while (g_server_running) {
        printf("🔄 클라이언트 연결을 기다립니다... (포트 %d)\n", port);
        
        socket_t client_socket = accept(server_socket, 
            (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (client_socket == INVALID_SOCKET) {
            if (g_server_running) {
                write_error_log("start_server", "Failed to accept client connection");
            }
            continue;
        }
        
        client_counter++;
        printf("✅ 클라이언트 %d가 연결되었습니다! (총 %d번째 연결)\n", 
               client_counter, client_counter);
        
        // 스레드 데이터 준비
        ClientThreadData* thread_data = malloc(sizeof(ClientThreadData));
        if (!thread_data) {
            printf("❌ 메모리 할당 실패\n");
            closesocket(client_socket);
            continue;
        }
        
        thread_data->client_socket = client_socket;
        thread_data->client_id = client_counter;
        
        // 새 스레드에서 클라이언트 처리
#ifdef _WIN32
        HANDLE thread = CreateThread(NULL, 0, handle_client_thread, thread_data, 0, NULL);
        if (thread == NULL) {
            printf("❌ 스레드 생성 실패\n");
            closesocket(client_socket);
            free(thread_data);
        } else {
            CloseHandle(thread);  // 스레드 핸들 정리 (detached 모드)
            printf("🧵 클라이언트 %d 처리 스레드 생성 완료\n", client_counter);
        }
#else
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client_thread, thread_data) != 0) {
            printf("❌ 스레드 생성 실패\n");
            close(client_socket);
            free(thread_data);
        } else {
            pthread_detach(thread);  // 스레드 detach (자동 정리)
            printf("🧵 클라이언트 %d 처리 스레드 생성 완료\n", client_counter);
        }
#endif
    }
    
    // 서버 소켓 정리
#ifdef _WIN32
    closesocket(server_socket);
#else
    close(server_socket);
#endif
    
    write_log("INFO", "Server stopped");
    return 1;
}

// 서버 정리
void cleanup_server(void) {
    write_log("INFO", "Cleaning up server resources...");
    
#ifdef _WIN32
    DeleteCriticalSection(&g_server_data.data_mutex);
    DeleteCriticalSection(&g_server_data.client_mutex);
    WSACleanup();
#else
    pthread_mutex_destroy(&g_server_data.data_mutex);
    pthread_mutex_destroy(&g_server_data.client_mutex);
#endif
    
    write_log("INFO", "Server cleanup completed");
}

// 선거 데이터를 파일로 저장
int save_elections_to_file(ElectionInfo elections[], int count) {
    FILE* file = fopen(ELECTIONS_FILE, "w");
    if (!file) {
        write_error_log("save_elections_to_file", "파일 생성 실패");
        return 0;
    }
    
    fprintf(file, "# 선거 정보 데이터\n");
    fprintf(file, "# 형식: ID|이름|날짜|타입|활성상태\n");
    fprintf(file, "COUNT=%d\n", count);
    
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s|%s|%s|%s|%d\n",
                elections[i].election_id,
                elections[i].election_name,
                elections[i].election_date,
                elections[i].election_type,
                elections[i].is_active);
    }
    
    fclose(file);
    printf("✅ 선거 정보 %d개를 %s에 저장했습니다.\n", count, ELECTIONS_FILE);
    return 1;
}

// 후보자 데이터를 파일로 저장
int save_candidates_to_file(CandidateInfo candidates[], int count) {
    FILE* file = fopen(CANDIDATES_FILE, "w");
    if (!file) {
        write_error_log("save_candidates_to_file", "파일 생성 실패");
        return 0;
    }
    
    fprintf(file, "# 후보자 정보 데이터\n");
    fprintf(file, "# 형식: 후보자ID|이름|정당|번호|선거ID|공약수\n");
    fprintf(file, "COUNT=%d\n", count);
    
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s|%s|%s|%d|%s|%d\n",
                candidates[i].candidate_id,
                candidates[i].candidate_name,
                candidates[i].party_name,
                candidates[i].candidate_number,
                candidates[i].election_id,
                candidates[i].pledge_count);
    }
    
    fclose(file);
    printf("✅ 후보자 정보 %d개를 %s에 저장했습니다.\n", count, CANDIDATES_FILE);
    return 1;
}

// 공약 데이터를 파일로 저장
int save_pledges_to_file(PledgeInfo pledges[], int count) {
    FILE* file = fopen(PLEDGES_FILE, "w");
    if (!file) {
        write_error_log("save_pledges_to_file", "파일 생성 실패");
        return 0;
    }
    
    fprintf(file, "# 공약 정보 데이터\n");
    fprintf(file, "# 형식: 공약ID|후보자ID|제목|내용|카테고리|좋아요|싫어요|생성시간\n");
    fprintf(file, "COUNT=%d\n", count);
    
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s|%s|%s|%s|%s|%d|%d|%lld\n",
                pledges[i].pledge_id,
                pledges[i].candidate_id,
                pledges[i].title,
                pledges[i].content,
                pledges[i].category,
                pledges[i].like_count,
                pledges[i].dislike_count,
                (long long)pledges[i].created_time);
    }
    
    fclose(file);
    printf("✅ 공약 정보 %d개를 %s에 저장했습니다.\n", count, PLEDGES_FILE);
    return 1;
}

// 업데이트 시간 저장
void save_update_time(void) {
    FILE* file = fopen(UPDATE_TIME_FILE, "w");
    if (!file) return;
    
    time_t now = time(NULL);
    fprintf(file, "%lld\n", (long long)now);
    fprintf(file, "%s", ctime(&now));
    fclose(file);
}

// 서버 시작 시 API 데이터 수집
// 선거 정보만 수집하는 함수
int collect_elections_only(void) {
    printf("\n🔄 선거 정보만 수집을 시작합니다...\n");
    fflush(stdout);
    
    // 뮤텍스 잠금
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    printf("🔒 API 호출 뮤텍스 잠금 획득\n");
    fflush(stdout);
    
    // 동적 메모리 할당
    APIClient* api_client = malloc(sizeof(APIClient));
    ElectionInfo* elections = malloc(sizeof(ElectionInfo) * MAX_ELECTIONS);
    char* response_buffer = malloc(65536);
    
    if (!api_client || !elections || !response_buffer) {
        printf("❌ 메모리 할당 실패\n");
        fflush(stdout);
        goto cleanup_memory;
    }
    
    memset(api_client, 0, sizeof(APIClient));
    memset(elections, 0, sizeof(ElectionInfo) * MAX_ELECTIONS);
    
    int election_count = 0;
    int success = 1;
    
    // API 클라이언트 초기화
    printf("🔧 API 클라이언트 초기화 중...\n");
    fflush(stdout);
    
    if (!init_api_client(api_client)) {
        printf("❌ API 클라이언트 초기화 실패\n");
        fflush(stdout);
        success = 0;
        goto cleanup;
    }
    
    printf("✅ API 클라이언트 초기화 완료\n");
    fflush(stdout);
    
    // 선거 정보 수집
    printf("\n📊 선거 정보 수집 중...\n");
    fflush(stdout);
    
    if (api_get_election_info(api_client, response_buffer, 65536) == 0) {
        printf("✅ 선거 정보 API 호출 성공\n");
        fflush(stdout);
        
        election_count = parse_election_json(response_buffer, elections, MAX_ELECTIONS);
        printf("📊 파싱된 선거 정보: %d개\n", election_count);
        fflush(stdout);
        
        if (election_count > 0) {
            if (save_elections_to_file(elections, election_count)) {
                printf("✅ 선거 정보 저장 완료\n");
            } else {
                printf("⚠️ 선거 정보 저장 실패\n");
            }
        }
    } else {
        printf("⚠️ 선거 정보 API 호출 실패\n");
        fflush(stdout);
        success = 0;
    }
    
    save_update_time();
    
cleanup:
    if (api_client && api_client->is_initialized) {
        cleanup_api_client(api_client);
    }
    
    printf("\n🎉 선거 정보 수집 완료!\n");
    printf("   - 선거 정보: %d개\n", election_count);
    fflush(stdout);
    
    // 서버 전역 데이터 업데이트
    g_server_data.election_count = load_elections_from_file(g_server_data.elections, MAX_ELECTIONS);
    
cleanup_memory:
    if (api_client) free(api_client);
    if (elections) free(elections);
    if (response_buffer) free(response_buffer);
    
    printf("🔓 API 호출 뮤텍스 해제\n");
    fflush(stdout);
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return success;
}

// 후보자 정보만 수집하는 함수
int collect_candidates_only(void) {
    printf("\n🔄 후보자 정보만 수집을 시작합니다...\n");
    fflush(stdout);
    
    // 뮤텍스 잠금
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    printf("🔒 API 호출 뮤텍스 잠금 획득\n");
    fflush(stdout);
    
    // 동적 메모리 할당
    APIClient* api_client = malloc(sizeof(APIClient));
    ElectionInfo* elections = malloc(sizeof(ElectionInfo) * MAX_ELECTIONS);
    CandidateInfo* candidates = malloc(sizeof(CandidateInfo) * MAX_CANDIDATES);
    char* response_buffer = malloc(65536);
    
    if (!api_client || !elections || !candidates || !response_buffer) {
        printf("❌ 메모리 할당 실패\n");
        fflush(stdout);
        goto cleanup_memory;
    }
    
    memset(api_client, 0, sizeof(APIClient));
    memset(elections, 0, sizeof(ElectionInfo) * MAX_ELECTIONS);
    memset(candidates, 0, sizeof(CandidateInfo) * MAX_CANDIDATES);
    
    int total_candidates = 0;
    int success = 1;
    
    // API 클라이언트 초기화
    printf("🔧 API 클라이언트 초기화 중...\n");
    fflush(stdout);
    
    if (!init_api_client(api_client)) {
        printf("❌ API 클라이언트 초기화 실패\n");
        fflush(stdout);
        success = 0;
        goto cleanup;
    }
    
    printf("✅ API 클라이언트 초기화 완료\n");
    fflush(stdout);
    
    // 기존 선거 정보 로드
    int election_count = load_elections_from_file(elections, MAX_ELECTIONS);
    printf("📂 기존 선거 정보 %d개 로드\n", election_count);
    
    if (election_count == 0) {
        printf("⚠️ 선거 정보가 없습니다. 먼저 선거 정보를 새로고침하세요.\n");
        success = 0;
        goto cleanup;
    }
    
    // 후보자 정보 수집
    printf("\n👥 후보자 정보 수집 중...\n");
    fflush(stdout);
    
    // 현재 시간 확인 (미래 선거 제외)
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    int current_year = tm_now->tm_year + 1900;
    
    // 실제 열린 선거만 처리 (미래 선거 제외)
    int processed_elections = 0;
    int max_elections_to_process = 3; // 최대 3개 선거까지 처리
    
    for (int i = 0; i < election_count && processed_elections < max_elections_to_process && total_candidates < MAX_CANDIDATES - 100; i++) {
        // 선거 연도 확인
        int election_year = atoi(elections[i].election_id) / 10000;
        if (election_year > current_year) {
            printf("   ⚠️  미래 선거 건너뛰기: %s (%d년)\n", elections[i].election_name, election_year);
            continue;
        }
        
        printf("   선거 %d/%d: %s 처리 중...\n", 
               processed_elections, max_elections_to_process, elections[i].election_name);
        fflush(stdout);
        
        if (api_get_candidate_info(api_client, elections[i].election_id, 
                                  response_buffer, 65536) == 0) {
            printf("   ✅ 후보자 API 호출 성공\n");
            fflush(stdout);
            
            int count = parse_candidate_json(response_buffer, elections[i].election_id,
                                           &candidates[total_candidates], 
                                           MAX_CANDIDATES - total_candidates);
            if (count > 0) {
                printf("   ✅ %d명 후보자 파싱 완료\n", count);
                total_candidates += count;
            }
            } else {
            printf("   ⚠️ 후보자 API 호출 실패, 건너뛰기\n");
        }
        fflush(stdout);
        
        if (i < max_elections_to_process - 1) {
            printf("   ⏳ 0.3초 대기 중...\n");
            fflush(stdout);
            Sleep(300);
        }
    }
    
    if (total_candidates > 0) {
        if (save_candidates_to_file(candidates, total_candidates)) {
            printf("✅ 후보자 정보 저장 완료\n");
        } else {
            printf("⚠️ 후보자 정보 저장 실패\n");
        }
    }
    
    save_update_time();
    
cleanup:
    if (api_client && api_client->is_initialized) {
        cleanup_api_client(api_client);
    }
    
    printf("\n🎉 후보자 정보 수집 완료!\n");
    printf("   - 후보자 정보: %d개\n", total_candidates);
    fflush(stdout);
    
    // 서버 전역 데이터 업데이트
    g_server_data.candidate_count = load_candidates_from_file(g_server_data.candidates, MAX_CANDIDATES);
    
cleanup_memory:
    if (api_client) free(api_client);
    if (elections) free(elections);
    if (candidates) free(candidates);
    if (response_buffer) free(response_buffer);
    
    printf("🔓 API 호출 뮤텍스 해제\n");
    fflush(stdout);
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return success;
}

// 공약 정보만 수집하는 함수
int collect_pledges_only(void) {
    printf("\n🔄 공약 정보만 수집을 시작합니다...\n");
    fflush(stdout);
    
    // 뮤텍스 잠금
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    printf("🔒 API 호출 뮤텍스 잠금 획득\n");
    fflush(stdout);
    
    // 동적 메모리 할당
    APIClient* api_client = malloc(sizeof(APIClient));
    CandidateInfo* candidates = malloc(sizeof(CandidateInfo) * MAX_CANDIDATES);
    PledgeInfo* pledges = malloc(sizeof(PledgeInfo) * MAX_PLEDGES);
    char* response_buffer = malloc(65536);
    
    if (!api_client || !candidates || !pledges || !response_buffer) {
        printf("❌ 메모리 할당 실패\n");
        fflush(stdout);
        goto cleanup_memory;
    }
    
    memset(api_client, 0, sizeof(APIClient));
    memset(candidates, 0, sizeof(CandidateInfo) * MAX_CANDIDATES);
    memset(pledges, 0, sizeof(PledgeInfo) * MAX_PLEDGES);
    
    int total_pledges = 0;
    int success = 1;
    
    // API 클라이언트 초기화
    printf("🔧 API 클라이언트 초기화 중...\n");
    fflush(stdout);
    
    if (!init_api_client(api_client)) {
        printf("❌ API 클라이언트 초기화 실패\n");
        fflush(stdout);
        success = 0;
        goto cleanup;
    }
    
    printf("✅ API 클라이언트 초기화 완료\n");
    fflush(stdout);
    
    // 기존 후보자 정보 로드
    int candidate_count = load_candidates_from_file(candidates, MAX_CANDIDATES);
    printf("📂 기존 후보자 정보 %d개 로드\n", candidate_count);
    
    if (candidate_count == 0) {
        printf("⚠️ 후보자 정보가 없습니다. 먼저 후보자 정보를 새로고침하세요.\n");
        success = 0;
        goto cleanup;
    }
    
    // 공약 정보 수집
    printf("\n📋 공약 정보 수집 중...\n");
    fflush(stdout);
    
    // 2017년 이후 후보자 목록 생성 (공약 데이터가 있는 후보자들)
    int valid_candidates[MAX_CANDIDATES];
    int valid_count = 0;
    
    for (int i = 0; i < candidate_count; i++) {
        int election_year = atoi(candidates[i].election_id) / 10000;
        if (election_year >= 2017) {
            valid_candidates[valid_count] = i;
            valid_count++;
            printf("🔍 공약 수집 대상 후보자 %d: %s (%s년)\n", 
                   valid_count, candidates[i].candidate_name, candidates[i].election_id);
        }
    }
    
    if (valid_count == 0) {
        printf("⚠️ 공약 데이터가 있는 후보자를 찾을 수 없습니다.\n");
        goto cleanup;
    }
    
    printf("📊 총 %d명의 후보자에 대해 공약 수집을 시작합니다.\n", valid_count);
    fflush(stdout);
    
    // 모든 유효한 후보자의 공약 수집
    for (int idx = 0; idx < valid_count && total_pledges < MAX_PLEDGES - 100; idx++) {
        int i = valid_candidates[idx];
        printf("   후보자 %d/%d: '%s' (ID: %s, 선거: %s) 공약 수집 중...\n", 
               idx+1, valid_count, candidates[i].candidate_name, 
               candidates[i].candidate_id, candidates[i].election_id);
        fflush(stdout);
        
        if (api_get_pledge_info(api_client, candidates[i].election_id, candidates[i].candidate_id,
                               response_buffer, 65536) == 0) {
            printf("   ✅ 공약 API 호출 성공 (응답 길이: %zu bytes)\n", strlen(response_buffer));
            fflush(stdout);
            
            // 응답 데이터 일부 출력 (디버깅용)
            printf("   📄 API 응답 일부: %.200s...\n", response_buffer);
            fflush(stdout);
            
            int count = parse_pledge_json(response_buffer, 
                                        &pledges[total_pledges], 
                                        MAX_PLEDGES - total_pledges);
            if (count > 0) {
                printf("   ✅ %d개 공약 파싱 완료\n", count);
                total_pledges += count;
            } else {
                printf("   ⚠️ 공약 파싱 결과 0개 - API 응답 확인 필요\n");
            }
        } else {
            printf("   ⚠️ 공약 API 호출 실패, 건너뛰기\n");
        }
        
        // API 호출 간 대기 (서버 부하 방지)
        if (idx < valid_count - 1) {
            Sleep(300); // 0.3초 대기
        }
        fflush(stdout);
    }
    
    if (total_pledges > 0) {
        if (save_pledges_to_file(pledges, total_pledges)) {
            printf("✅ 공약 정보 저장 완료\n");
            fflush(stdout);
            
            // 파일 저장 후 잠시 대기 (버퍼링 문제 해결)
            Sleep(100);
        } else {
            printf("⚠️ 공약 정보 저장 실패\n");
        }
    }
    
    save_update_time();
    
cleanup:
    if (api_client && api_client->is_initialized) {
        cleanup_api_client(api_client);
    }
    
    printf("\n🎉 공약 정보 수집 완료!\n");
    printf("   - 공약 정보: %d개\n", total_pledges);
    fflush(stdout);
    
    // 서버 전역 데이터 업데이트 (파일 저장 후 다시 로드)
    printf("🔄 공약 데이터 다시 로드 중...\n");
    fflush(stdout);
    g_server_data.pledge_count = load_pledges_from_file(g_server_data.pledges, MAX_PLEDGES);
    printf("📂 공약 정보 %d개 다시 로드 완료\n", g_server_data.pledge_count);
    fflush(stdout);
    
cleanup_memory:
    if (api_client) free(api_client);
    if (candidates) free(candidates);
    if (pledges) free(pledges);
    if (response_buffer) free(response_buffer);
    
    printf("🔓 API 호출 뮤텍스 해제\n");
    fflush(stdout);
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return success;
}

int collect_api_data(void) {
    printf("\n🔄 API 데이터 수집을 시작합니다...\n");
    fflush(stdout);
    
    // 뮤텍스 잠금 (한 번에 하나의 API 호출만 허용)
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    printf("🔒 API 호출 뮤텍스 잠금 획득\n");
    fflush(stdout);
    
    // 동적 메모리 할당 (스택 오버플로우 방지)
    APIClient* api_client = malloc(sizeof(APIClient));
    ElectionInfo* elections = malloc(sizeof(ElectionInfo) * MAX_ELECTIONS);
    CandidateInfo* candidates = malloc(sizeof(CandidateInfo) * MAX_CANDIDATES);
    PledgeInfo* pledges = malloc(sizeof(PledgeInfo) * MAX_PLEDGES);
    char* response_buffer = malloc(65536);
    
    if (!api_client || !elections || !candidates || !pledges || !response_buffer) {
        printf("❌ 메모리 할당 실패\n");
        fflush(stdout);
        goto cleanup_memory;
    }
    
    // 변수 초기화
    memset(api_client, 0, sizeof(APIClient));
    memset(elections, 0, sizeof(ElectionInfo) * MAX_ELECTIONS);
    memset(candidates, 0, sizeof(CandidateInfo) * MAX_CANDIDATES);
    memset(pledges, 0, sizeof(PledgeInfo) * MAX_PLEDGES);
    
    int election_count = 0;
    int total_candidates = 0;
    int total_pledges = 0;
    int success = 1;
    
    // API 클라이언트 초기화
    printf("🔧 API 클라이언트 초기화 중...\n");
    fflush(stdout);
    
    if (!init_api_client(api_client)) {
        printf("❌ API 클라이언트 초기화 실패\n");
        fflush(stdout);
        success = 0;
        goto cleanup;
    }
    
    printf("✅ API 클라이언트 초기화 완료\n");
    fflush(stdout);
    
    // 1. 선거 정보 수집
    printf("\n📊 선거 정보 수집 중...\n");
    fflush(stdout);
    
    if (api_get_election_info(api_client, response_buffer, 65536) == 0) {
        printf("✅ 선거 정보 API 호출 성공\n");
        fflush(stdout);
        
        election_count = parse_election_json(response_buffer, elections, MAX_ELECTIONS);
        printf("📊 파싱된 선거 정보: %d개\n", election_count);
        fflush(stdout);
        
        if (election_count > 0) {
            if (save_elections_to_file(elections, election_count)) {
                printf("✅ 선거 정보 저장 완료\n");
            } else {
                printf("⚠️ 선거 정보 저장 실패\n");
            }
        }
    } else {
        printf("⚠️ 선거 정보 API 호출 실패\n");
        fflush(stdout);
    }
    
    // 2. 후보자 정보 수집 (제한적으로)
    printf("\n👥 후보자 정보 수집 중...\n");
    fflush(stdout);
    
    // 최대 2개 선거만 처리 (더 안전하게)
    int max_elections_to_process = (election_count > 2) ? 2 : election_count;
    
    for (int i = 0; i < max_elections_to_process && total_candidates < MAX_CANDIDATES - 100; i++) {
        printf("   선거 %d/%d: %s 처리 중...\n", 
               i+1, max_elections_to_process, elections[i].election_name);
        fflush(stdout);
        
        if (api_get_candidate_info(api_client, elections[i].election_id, 
                                  response_buffer, 65536) == 0) {
            printf("   ✅ 후보자 API 호출 성공\n");
            fflush(stdout);
            
            int count = parse_candidate_json(response_buffer, elections[i].election_id,
                                           &candidates[total_candidates], 
                                           MAX_CANDIDATES - total_candidates);
            if (count > 0) {
                printf("   ✅ %d명 후보자 파싱 완료\n", count);
                total_candidates += count;
            }
        } else {
            printf("   ⚠️ 후보자 API 호출 실패, 건너뛰기\n");
        }
        fflush(stdout);
        
        // API 호출 간격
        if (i < max_elections_to_process - 1) {
            printf("   ⏳ 0.3초 대기 중...\n");
            fflush(stdout);
            Sleep(300);
        }
    }
    
    if (total_candidates > 0) {
        if (save_candidates_to_file(candidates, total_candidates)) {
            printf("✅ 후보자 정보 저장 완료\n");
        } else {
            printf("⚠️ 후보자 정보 저장 실패\n");
        }
    }
    
    // 3. 공약 정보 수집 (모든 2017년 이후 후보자)
    printf("\n📋 공약 정보 수집 중...\n");
    fflush(stdout);
    
    // 2017년 이후 후보자 목록 생성 (공약 데이터가 있는 후보자들)
    int valid_candidates[MAX_CANDIDATES];
    int valid_count = 0;
    
    for (int i = 0; i < total_candidates; i++) {
        int election_year = atoi(candidates[i].election_id) / 10000;
        if (election_year >= 2017) {
            valid_candidates[valid_count] = i;
            valid_count++;
            printf("🔍 공약 수집 대상 후보자 %d: %s (%s년)\n", 
                   valid_count, candidates[i].candidate_name, candidates[i].election_id);
        }
    }
    
    if (valid_count == 0) {
        printf("⚠️ 공약 데이터가 있는 후보자를 찾을 수 없습니다.\n");
        goto cleanup;
    }
    
    printf("📊 총 %d명의 후보자에 대해 공약 수집을 시작합니다.\n", valid_count);
    fflush(stdout);
    
    // 모든 유효한 후보자의 공약 수집
    for (int idx = 0; idx < valid_count && total_pledges < MAX_PLEDGES - 100; idx++) {
        int i = valid_candidates[idx];
        printf("   후보자 %d/%d: '%s' (ID: %s, 선거: %s) 공약 수집 중...\n", 
               idx+1, valid_count, candidates[i].candidate_name, 
               candidates[i].candidate_id, candidates[i].election_id);
        fflush(stdout);
        
        if (api_get_pledge_info(api_client, candidates[i].election_id, candidates[i].candidate_id,
                               response_buffer, 65536) == 0) {
            printf("   ✅ 공약 API 호출 성공 (응답 길이: %zu bytes)\n", strlen(response_buffer));
            fflush(stdout);
            
            // 응답 데이터 일부 출력 (디버깅용)  
            printf("   📄 API 응답 일부: %.200s...\n", response_buffer);
            fflush(stdout);
            
            int count = parse_pledge_json(response_buffer, 
                                        &pledges[total_pledges], 
                                        MAX_PLEDGES - total_pledges);
            if (count > 0) {
                printf("   ✅ %d개 공약 파싱 완료\n", count);
                total_pledges += count;
            } else {
                printf("   ⚠️ 공약 파싱 결과 0개 - API 응답 확인 필요\n");
            }
        } else {
            printf("   ⚠️ 공약 API 호출 실패, 건너뛰기\n");
        }
        
        // API 호출 간 대기 (서버 부하 방지)
        if (idx < valid_count - 1) {
            Sleep(300); // 0.3초 대기
        }
        fflush(stdout);
    }
    
    if (total_pledges > 0) {
        if (save_pledges_to_file(pledges, total_pledges)) {
            printf("✅ 공약 정보 저장 완료\n");
            fflush(stdout);
            
            // 파일 저장 후 잠시 대기 (버퍼링 문제 해결)
            Sleep(100);
        } else {
            printf("⚠️ 공약 정보 저장 실패\n");
        }
    }
    
    // 정리 작업
    save_update_time();
    
cleanup:
    // API 클라이언트 정리
    if (api_client && api_client->is_initialized) {
        cleanup_api_client(api_client);
    }
    
    printf("\n🎉 API 데이터 수집 완료!\n");
    printf("   - 선거 정보: %d개\n", election_count);
    printf("   - 후보자 정보: %d개\n", total_candidates);
    printf("   - 공약 정보: %d개\n", total_pledges);
    fflush(stdout);
    
    // 서버 전역 데이터 업데이트 (파일 저장 후 다시 로드)
    printf("🔄 전체 데이터 다시 로드 중...\n");
    fflush(stdout);
    g_server_data.election_count = load_elections_from_file(g_server_data.elections, MAX_ELECTIONS);
    g_server_data.candidate_count = load_candidates_from_file(g_server_data.candidates, MAX_CANDIDATES);
    g_server_data.pledge_count = load_pledges_from_file(g_server_data.pledges, MAX_PLEDGES);
    printf("📂 전체 데이터 로드 완료: 선거 %d개, 후보자 %d개, 공약 %d개\n", 
           g_server_data.election_count, g_server_data.candidate_count, g_server_data.pledge_count);
    fflush(stdout);
    
cleanup_memory:
    // 메모리 해제
    if (api_client) free(api_client);
    if (elections) free(elections);
    if (candidates) free(candidates);
    if (pledges) free(pledges);
    if (response_buffer) free(response_buffer);
    
    // 뮤텍스 해제
    printf("🔓 API 호출 뮤텍스 해제\n");
    fflush(stdout);
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return 1; // 항상 성공으로 반환하여 서버 크래시 방지
}

// 파일에서 선거 데이터 읽기
int load_elections_from_file(ElectionInfo elections[], int max_count) {
    FILE* file = fopen(ELECTIONS_FILE, "r");
    if (!file) {
        write_error_log("load_elections_from_file", "파일 열기 실패");
        return 0;
    }
    
    char line[1024];
    int count = 0;
    int data_count = 0;
    
    while (fgets(line, sizeof(line), file) && count < max_count) {
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // COUNT 라인 처리
        if (strncmp(line, "COUNT=", 6) == 0) {
            data_count = atoi(line + 6);
            continue;
        }
        
        // 데이터 파싱: ID|이름|날짜|타입|활성상태
        char* token = strtok(line, "|");
        if (!token) continue;
        
        strncpy(elections[count].election_id, token, sizeof(elections[count].election_id) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(elections[count].election_name, token, sizeof(elections[count].election_name) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(elections[count].election_date, token, sizeof(elections[count].election_date) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(elections[count].election_type, token, sizeof(elections[count].election_type) - 1);
        
        token = strtok(NULL, "|\n");
        if (!token) continue;
        elections[count].is_active = atoi(token);
        
        count++;
    }
    
    fclose(file);
    printf("📂 선거 정보 %d개를 파일에서 로드했습니다.\n", count);
    return count;
}

// 파일에서 후보자 데이터 읽기
int load_candidates_from_file(CandidateInfo candidates[], int max_count) {
    FILE* file = fopen(CANDIDATES_FILE, "r");
    if (!file) {
        write_error_log("load_candidates_from_file", "파일 열기 실패");
        return 0;
    }
    
    char line[1024];
    int count = 0;
    
    while (fgets(line, sizeof(line), file) && count < max_count) {
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, "COUNT=", 6) == 0) continue;
        
        // 데이터 파싱: 후보자ID|이름|정당|번호|선거ID|공약수
        char* token = strtok(line, "|");
        if (!token) continue;
        
        strncpy(candidates[count].candidate_id, token, sizeof(candidates[count].candidate_id) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(candidates[count].candidate_name, token, sizeof(candidates[count].candidate_name) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(candidates[count].party_name, token, sizeof(candidates[count].party_name) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        candidates[count].candidate_number = atoi(token);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(candidates[count].election_id, token, sizeof(candidates[count].election_id) - 1);
        
        token = strtok(NULL, "|\n");
        if (!token) continue;
        candidates[count].pledge_count = atoi(token);
        
        count++;
    }
    
    fclose(file);
    printf("📂 후보자 정보 %d개를 파일에서 로드했습니다.\n", count);
    return count;
}

// 파일에서 공약 데이터 읽기
int load_pledges_from_file(PledgeInfo pledges[], int max_count) {
    FILE* file = fopen(PLEDGES_FILE, "r");
    if (!file) {
        write_error_log("load_pledges_from_file", "파일 열기 실패");
        return 0;
    }
    
    char line[2048];
    int count = 0;
    int line_num = 0;
    
    while (fgets(line, sizeof(line), file) && count < max_count) {
        line_num++;
        
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        if (strncmp(line, "COUNT=", 6) == 0) {
            continue;
        }
        
        // 개행 문자 제거
        line[strcspn(line, "\n")] = 0;
        
        // 파이프(|) 개수 확인 - 정확히 7개여야 함 (8개 필드)
        int pipe_count = 0;
        for (int i = 0; line[i]; i++) {
            if (line[i] == '|') pipe_count++;
        }
        
        if (pipe_count != 7) {
            if (count <= 5) {  // 처음 5개만 로그 출력
                printf("DEBUG: 라인 %d 건너뛰기 (파이프 개수: %d개): %.50s...\n", 
                       line_num, pipe_count, line);
            }
            continue;
        }
        
        // 데이터 파싱: 공약ID|후보자ID|제목|내용|카테고리|좋아요|싫어요|생성시간
        // 빈 필드를 고려한 수동 파싱
        char* tokens[8];
        char line_copy[2048];
        strcpy(line_copy, line);
        
        int token_count = 0;
        char* start = line_copy;
        char* end;
        
        // 8개 필드를 순차적으로 파싱
        for (int i = 0; i < 8 && token_count < 8; i++) {
            end = strchr(start, '|');
            if (end) {
                *end = '\0';  // 구분자를 NULL로 변경
                tokens[token_count++] = start;
                start = end + 1;
            } else if (i == 7) {
                // 마지막 필드 (생성시간)
                tokens[token_count++] = start;
            } else {
                // 예상치 못한 상황
                break;
            }
        }
        
        if (token_count < 8) {
            continue;
        }
        
        // 필드 할당
        strncpy(pledges[count].pledge_id, tokens[0], sizeof(pledges[count].pledge_id) - 1);
        pledges[count].pledge_id[sizeof(pledges[count].pledge_id) - 1] = '\0';
        
        strncpy(pledges[count].candidate_id, tokens[1], sizeof(pledges[count].candidate_id) - 1);
        pledges[count].candidate_id[sizeof(pledges[count].candidate_id) - 1] = '\0';
        
        strncpy(pledges[count].title, tokens[2], sizeof(pledges[count].title) - 1);
        pledges[count].title[sizeof(pledges[count].title) - 1] = '\0';
        
        strncpy(pledges[count].content, tokens[3], sizeof(pledges[count].content) - 1);
        pledges[count].content[sizeof(pledges[count].content) - 1] = '\0';
        
        strncpy(pledges[count].category, tokens[4], sizeof(pledges[count].category) - 1);
        pledges[count].category[sizeof(pledges[count].category) - 1] = '\0';
        
        pledges[count].like_count = atoi(tokens[5]);
        pledges[count].dislike_count = atoi(tokens[6]);
        pledges[count].created_time = (time_t)atoll(tokens[7]);
        
        count++;
    }
    
    fclose(file);
    printf("📂 공약 정보 %d개를 파일에서 로드했습니다.\n", count);
    return count;
}

// 업데이트 시간 확인
time_t get_last_update_time(void) {
    FILE* file = fopen(UPDATE_TIME_FILE, "r");
    if (!file) return 0;
    
    time_t update_time = 0;
    fscanf(file, "%lld", (long long*)&update_time);
    fclose(file);
    
    return update_time;
}

// =====================================================
// 공약 평가 시스템 구현
// =====================================================

// 공약 평가 요청 처리 (새로운 평가 또는 기존 평가 변경)
void handle_evaluate_pledge_request(const char* user_id, const char* pledge_id, int evaluation_type, NetworkMessage* response) {
    if (!user_id || !pledge_id || !response) {
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "잘못된 매개변수입니다.");
        return;
    }
    
    // 평가 타입 검증 (1: 좋아요, -1: 싫어요)
    if (evaluation_type != 1 && evaluation_type != -1) {
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "잘못된 평가 타입입니다. (1: 좋아요, -1: 싫어요)");
        return;
    }
    
    printf("🔍 평가 요청 처리: 사용자=%s, 공약=%s, 타입=%d\n", user_id, pledge_id, evaluation_type);
    write_log("INFO", "공약 평가 요청 처리 시작");
    
    // 기존 평가 확인
    int existing_evaluation = get_user_evaluation(user_id, pledge_id);
    
    if (existing_evaluation == evaluation_type) {
        // 동일한 평가를 다시 시도하는 경우
        response->status_code = STATUS_BAD_REQUEST;
        snprintf(response->data, sizeof(response->data), 
                "이미 해당 공약에 %s 평가를 하셨습니다.", 
                evaluation_type == 1 ? "좋아요" : "싫어요");
        return;
    }
    
    // 평가 추가/변경
    if (update_evaluation(user_id, pledge_id, evaluation_type)) {
        // 통계 업데이트
        update_pledge_statistics(pledge_id);
        
        response->status_code = STATUS_SUCCESS;
        if (existing_evaluation == 0) {
            // 새로운 평가
        snprintf(response->data, sizeof(response->data), 
                "공약 평가가 성공적으로 등록되었습니다. (평가: %s)", 
                evaluation_type == 1 ? "좋아요" : "싫어요");
        } else {
            // 평가 변경
            snprintf(response->data, sizeof(response->data), 
                    "공약 평가가 %s에서 %s로 변경되었습니다.", 
                    existing_evaluation == 1 ? "좋아요" : "싫어요",
                    evaluation_type == 1 ? "좋아요" : "싫어요");
        }
        
        write_access_log(user_id, "공약 평가 완료");
    } else {
        response->status_code = STATUS_INTERNAL_ERROR;
        strcpy(response->data, "평가 등록 중 오류가 발생했습니다.");
        write_error_log("handle_evaluate_pledge_request", "평가 추가/변경 실패");
    }
}

// 평가 추가
int add_evaluation(const char* user_id, const char* pledge_id, int evaluation_type) {
    if (!user_id || !pledge_id) return 0;
    
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    // 평가 배열이 가득 찬 경우 확인
    if (g_server_data.evaluation_count >= 10000) {
        write_error_log("add_evaluation", "평가 저장 공간 부족");
#ifdef _WIN32
        LeaveCriticalSection(&g_server_data.data_mutex);
#else
        pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
        return 0;
    }
    
    // 새 평가 정보 추가
    EvaluationInfo* eval = &g_server_data.evaluations[g_server_data.evaluation_count];
    strcpy(eval->user_id, user_id);
    strcpy(eval->pledge_id, pledge_id);
    eval->evaluation_type = evaluation_type;
    eval->evaluation_time = time(NULL);
    
    g_server_data.evaluation_count++;
    
    // 평가 데이터를 파일에 저장
    FILE* file = fopen("data/evaluations.txt", "a");
    if (file) {
        fprintf(file, "%s|%s|%d|%lld\n", 
                eval->user_id, eval->pledge_id, eval->evaluation_type, 
                (long long)eval->evaluation_time);
        fclose(file);
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    write_log("INFO", "새 평가 추가 완료");
    return 1;
}

// 사용자의 특정 공약에 대한 평가 조회
int get_user_evaluation(const char* user_id, const char* pledge_id) {
    if (!user_id || !pledge_id) return 0;
    
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    for (int i = 0; i < g_server_data.evaluation_count; i++) {
        if (strcmp(g_server_data.evaluations[i].user_id, user_id) == 0 &&
            strcmp(g_server_data.evaluations[i].pledge_id, pledge_id) == 0) {
            int evaluation_type = g_server_data.evaluations[i].evaluation_type;
#ifdef _WIN32
            LeaveCriticalSection(&g_server_data.data_mutex);
#else
            pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
            return evaluation_type; // 1: 좋아요, -1: 싫어요, 0: 취소됨
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return 0; // 평가 없음
}

// 평가 추가/변경 (기존 평가가 있으면 변경, 없으면 추가)
int update_evaluation(const char* user_id, const char* pledge_id, int evaluation_type) {
    if (!user_id || !pledge_id) return 0;
    
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    // 기존 평가 찾기
    for (int i = 0; i < g_server_data.evaluation_count; i++) {
        if (strcmp(g_server_data.evaluations[i].user_id, user_id) == 0 &&
            strcmp(g_server_data.evaluations[i].pledge_id, pledge_id) == 0) {
            // 기존 평가 변경
            g_server_data.evaluations[i].evaluation_type = evaluation_type;
            g_server_data.evaluations[i].evaluation_time = time(NULL);
            
            // 파일에 전체 평가 데이터 다시 저장
            save_evaluations_to_file();
            
#ifdef _WIN32
            LeaveCriticalSection(&g_server_data.data_mutex);
#else
            pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
            write_log("INFO", "기존 평가 변경 완료");
            return 1;
        }
    }
    
    // 새 평가 추가
    if (g_server_data.evaluation_count >= 10000) {
        write_error_log("update_evaluation", "평가 저장 공간 부족");
#ifdef _WIN32
        LeaveCriticalSection(&g_server_data.data_mutex);
#else
        pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
        return 0;
    }
    
    EvaluationInfo* eval = &g_server_data.evaluations[g_server_data.evaluation_count];
    strcpy(eval->user_id, user_id);
    strcpy(eval->pledge_id, pledge_id);
    eval->evaluation_type = evaluation_type;
    eval->evaluation_time = time(NULL);
    
    g_server_data.evaluation_count++;
    
    // 파일에 전체 평가 데이터 저장
    save_evaluations_to_file();
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    write_log("INFO", "새 평가 추가 완료");
    return 1;
}

// 평가 취소
int cancel_evaluation(const char* user_id, const char* pledge_id) {
    if (!user_id || !pledge_id) return 0;
    
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    // 기존 평가 찾아서 제거
    for (int i = 0; i < g_server_data.evaluation_count; i++) {
        if (strcmp(g_server_data.evaluations[i].user_id, user_id) == 0 &&
            strcmp(g_server_data.evaluations[i].pledge_id, pledge_id) == 0) {
            
            // 배열에서 해당 평가 제거 (뒤의 요소들을 앞으로 이동)
            for (int j = i; j < g_server_data.evaluation_count - 1; j++) {
                g_server_data.evaluations[j] = g_server_data.evaluations[j + 1];
            }
            g_server_data.evaluation_count--;
            
            // 파일에 전체 평가 데이터 다시 저장
            save_evaluations_to_file();
            
#ifdef _WIN32
            LeaveCriticalSection(&g_server_data.data_mutex);
#else
            pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
            write_log("INFO", "평가 취소 완료");
            return 1;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return 0; // 취소할 평가가 없음
}

// 평가 취소 요청 처리
void handle_cancel_evaluation_request(const char* user_id, const char* pledge_id, NetworkMessage* response) {
    if (!user_id || !pledge_id || !response) {
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "잘못된 매개변수입니다.");
        return;
    }
    
    write_log("INFO", "평가 취소 요청 처리 시작");
    
    // 기존 평가 확인
    int existing_evaluation = get_user_evaluation(user_id, pledge_id);
    if (existing_evaluation == 0) {
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "취소할 평가가 없습니다.");
        return;
    }
    
    // 평가 취소
    if (cancel_evaluation(user_id, pledge_id)) {
        // 통계 업데이트
        update_pledge_statistics(pledge_id);
        
        response->status_code = STATUS_SUCCESS;
        snprintf(response->data, sizeof(response->data), 
                "%s 평가가 취소되었습니다.", 
                existing_evaluation == 1 ? "좋아요" : "싫어요");
        
        write_access_log(user_id, "평가 취소 완료");
    } else {
        response->status_code = STATUS_INTERNAL_ERROR;
        strcpy(response->data, "평가 취소 중 오류가 발생했습니다.");
        write_error_log("handle_cancel_evaluation_request", "평가 취소 실패");
    }
}

// 사용자 평가 조회 요청 처리
void handle_get_user_evaluation_request(const char* user_id, const char* pledge_id, NetworkMessage* response) {
    if (!user_id || !pledge_id || !response) {
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "잘못된 매개변수입니다.");
        return;
    }
    
    int evaluation = get_user_evaluation(user_id, pledge_id);
    
    // 단순히 평가 타입만 반환
    snprintf(response->data, sizeof(response->data), "%d", evaluation);
    
    response->status_code = STATUS_SUCCESS;
    write_log("INFO", "사용자 평가 조회 완료");
}

// 평가 데이터를 파일에 저장 (전체 덮어쓰기)
int save_evaluations_to_file(void) {
    printf("💾 평가 데이터 파일 저장 시작 (총 %d개 평가)\n", g_server_data.evaluation_count);
    
    FILE* file = fopen("data/evaluations.txt", "w");
    if (!file) {
        printf("❌ 평가 파일 열기 실패: data/evaluations.txt\n");
        write_error_log("save_evaluations_to_file", "파일 열기 실패");
        return 0;
    }
    
    fprintf(file, "# 평가 정보 데이터\n");
    fprintf(file, "# 형식: 사용자ID|공약ID|평가타입|평가시간\n");
    fprintf(file, "# 평가타입: 1=좋아요, -1=싫어요\n");
    
    for (int i = 0; i < g_server_data.evaluation_count; i++) {
        fprintf(file, "%s|%s|%d|%lld\n",
                g_server_data.evaluations[i].user_id,
                g_server_data.evaluations[i].pledge_id,
                g_server_data.evaluations[i].evaluation_type,
                (long long)g_server_data.evaluations[i].evaluation_time);
        printf("   📝 저장: %s|%s|%d\n", 
               g_server_data.evaluations[i].user_id,
               g_server_data.evaluations[i].pledge_id,
               g_server_data.evaluations[i].evaluation_type);
    }
    
    fclose(file);
    printf("✅ 평가 데이터 파일 저장 완료: %d개 평가\n", g_server_data.evaluation_count);
    write_log("INFO", "평가 데이터 파일 저장 완료");
    return 1;
}

// 중복 평가 확인
int check_duplicate_evaluation(const char* user_id, const char* pledge_id) {
    if (!user_id || !pledge_id) return 0;
    
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    for (int i = 0; i < g_server_data.evaluation_count; i++) {
        if (strcmp(g_server_data.evaluations[i].user_id, user_id) == 0 &&
            strcmp(g_server_data.evaluations[i].pledge_id, pledge_id) == 0) {
#ifdef _WIN32
            LeaveCriticalSection(&g_server_data.data_mutex);
#else
            pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
            return 1; // 중복 발견
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    return 0; // 중복 없음
}

// 공약 통계 업데이트
void update_pledge_statistics(const char* pledge_id) {
    if (!pledge_id) return;
    
    int like_count = 0;
    int dislike_count = 0;
    
#ifdef _WIN32
    EnterCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_lock(&g_server_data.data_mutex);
#endif
    
    // 해당 공약의 모든 평가 집계
    for (int i = 0; i < g_server_data.evaluation_count; i++) {
        if (strcmp(g_server_data.evaluations[i].pledge_id, pledge_id) == 0) {
            if (g_server_data.evaluations[i].evaluation_type == 1) {
                like_count++;
            } else if (g_server_data.evaluations[i].evaluation_type == -1) {
                dislike_count++;
            }
            // evaluation_type == 0인 경우는 취소된 평가이므로 집계하지 않음
        }
    }
    
    // 공약 정보에서 해당 공약 찾아서 통계 업데이트
    for (int i = 0; i < g_server_data.pledge_count; i++) {
        if (strcmp(g_server_data.pledges[i].pledge_id, pledge_id) == 0) {
            g_server_data.pledges[i].like_count = like_count;
            g_server_data.pledges[i].dislike_count = dislike_count;
            break;
        }
    }
    
#ifdef _WIN32
    LeaveCriticalSection(&g_server_data.data_mutex);
#else
    pthread_mutex_unlock(&g_server_data.data_mutex);
#endif
    
    write_log("INFO", "공약 통계 업데이트 완료");
}

// 공약 통계 요청 처리
void handle_get_statistics_request(const char* pledge_id, NetworkMessage* response) {
    if (!pledge_id || !response) {
        response->status_code = STATUS_BAD_REQUEST;
        strcpy(response->data, "잘못된 매개변수입니다.");
        return;
    }
    
    // 해당 공약 찾기
    PledgeInfo* pledge = NULL;
    for (int i = 0; i < g_server_data.pledge_count; i++) {
        if (strcmp(g_server_data.pledges[i].pledge_id, pledge_id) == 0) {
            pledge = &g_server_data.pledges[i];
            break;
        }
    }
    
    if (!pledge) {
        response->status_code = STATUS_NOT_FOUND;
        strcpy(response->data, "해당 공약을 찾을 수 없습니다.");
        return;
    }
    
    // 통계 정보를 JSON 형태로 생성
    double approval_rate = 0.0;
    int total_votes = pledge->like_count + pledge->dislike_count;
    if (total_votes > 0) {
        approval_rate = ((double)pledge->like_count / total_votes) * 100.0;
    }
    
    snprintf(response->data, sizeof(response->data),
        "{"
        "\"pledge_id\":\"%s\","
        "\"title\":\"%s\","
        "\"like_count\":%d,"
        "\"dislike_count\":%d,"
        "\"total_votes\":%d,"
        "\"approval_rate\":%.1f"
        "}",
        pledge->pledge_id,
        pledge->title,
        pledge->like_count,
        pledge->dislike_count,
        total_votes,
        approval_rate
    );
    
    response->status_code = STATUS_SUCCESS;
    write_log("INFO", "공약 통계 정보 제공 완료");
}

// 평가 데이터 파일 로드
int load_evaluations_from_file(void) {
    FILE* file = fopen("data/evaluations.txt", "r");
    if (!file) {
        write_log("WARNING", "평가 데이터 파일이 없습니다. 새로 생성됩니다.");
        return 0;
    }
    
    char line[512];
    g_server_data.evaluation_count = 0;
    
    while (fgets(line, sizeof(line), file) && 
           g_server_data.evaluation_count < 10000) {
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || line[0] == '\n') continue;
        
        // 데이터 파싱: 사용자ID|공약ID|평가타입|평가시간
        char* token = strtok(line, "|");
        if (!token) continue;
        
        EvaluationInfo* eval = &g_server_data.evaluations[g_server_data.evaluation_count];
        strcpy(eval->user_id, token);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strcpy(eval->pledge_id, token);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        eval->evaluation_type = atoi(token);
        
        token = strtok(NULL, "|\n");
        if (!token) continue;
        eval->evaluation_time = (time_t)atoll(token);
        
        g_server_data.evaluation_count++;
    }
    
    fclose(file);
    printf("📊 평가 데이터 %d개를 파일에서 로드했습니다.\n", g_server_data.evaluation_count);
    
    // 로드된 평가 데이터를 기반으로 모든 공약의 통계 업데이트
    printf("🔄 공약 통계 업데이트 중...\n");
    for (int i = 0; i < g_server_data.pledge_count; i++) {
        update_pledge_statistics(g_server_data.pledges[i].pledge_id);
    }
    printf("✅ 공약 통계 업데이트 완료!\n");
    
    // 업데이트된 통계를 파일에 저장
    printf("💾 업데이트된 공약 통계를 파일에 저장 중...\n");
    if (save_pledges_to_file(g_server_data.pledges, g_server_data.pledge_count)) {
        printf("✅ 공약 통계 파일 저장 완료!\n");
    } else {
        printf("❌ 공약 통계 파일 저장 실패!\n");
    }
    
    return g_server_data.evaluation_count;
}

// 메인 함수
int main(int argc, char* argv[]) {
    // EUC-KR 콘솔 초기화
    init_korean_console();
    
    // 랜덤 시드 초기화 (세션 ID 생성용)
    srand((unsigned int)time(NULL));
    
    int port = SERVER_PORT;
    
    // 명령행 인수 처리
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            printf("잘못된 포트 번호: %s\n", argv[1]);
            printf("사용법: %s [포트번호]\n", argv[0]);
            return 1;
        }
    }
    
    // 신호 처리 설정
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif
    
    print_header("대선 후보 공약 열람 및 평가 시스템 서버");
    printf("포트: %d\n", port);
    printf("종료하려면 Ctrl+C를 누르세요.\n");
    print_separator();
    
    // 서버 초기화
    if (!init_server()) {
        printf("서버 초기화 실패\n");
        return 1;
    }
    
    // API 데이터 수집 및 파일 저장 (주석 처리 - 안정성을 위해)
    /*
    if (!collect_api_data()) {
        printf("⚠️  API 데이터 수집에 실패했지만 서버를 계속 실행합니다.\n");
    }
    */
    
    printf("\n");
    print_separator();
    printf("서버 준비 완료! 클라이언트 연결을 기다립니다...\n");
    printf("💡 데이터 수집은 클라이언트에서 '데이터 새로고침'을 선택하세요.\n");
    print_separator();
    
    // 서버 시작
    if (!start_server(port)) {
        printf("서버 시작 실패\n");
        cleanup_server();
        return 1;
    }
    
    // 서버 정리
    cleanup_server();
    
    printf("서버가 종료되었습니다.\n");
    return 0;
} 