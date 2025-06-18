#ifndef SERVER_H
#define SERVER_H

#include "structures.h"
#include "utils.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCKET -1
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR -1
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
#endif

#include <signal.h>
#ifndef _WIN32
    #include <pthread.h>
#endif

// 서버 설정
#define SERVER_PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 4096
#define SESSION_TIMEOUT 3600  // 1시간

// 클라이언트 세션 정보
typedef struct {
    socket_t socket;
    char user_id[MAX_STRING_LEN];
    char session_id[MAX_STRING_LEN];
    time_t last_activity;
    int is_active;
#ifdef _WIN32
    HANDLE thread_handle;
    DWORD thread_id;
#else
    pthread_t thread_id;
#endif
} ClientSession;

// 서버 전역 데이터
typedef struct {
    UserInfo users[MAX_USERS];
    int user_count;
    
    ElectionInfo elections[MAX_ELECTIONS];
    int election_count;
    
    CandidateInfo candidates[MAX_CANDIDATES];
    int candidate_count;
    
    PledgeInfo pledges[MAX_PLEDGES];
    int pledge_count;
    
    EvaluationInfo evaluations[10000];  // 평가 데이터 최대 10,000개로 제한
    int evaluation_count;
    
    ClientSession clients[MAX_CLIENTS];
    int client_count;
    
#ifdef _WIN32
    CRITICAL_SECTION data_mutex;
    CRITICAL_SECTION client_mutex;
#else
    pthread_mutex_t data_mutex;
    pthread_mutex_t client_mutex;
#endif
} ServerData;

// 서버 초기화 및 종료
int init_server(void);
int start_server(int port);
void shutdown_server(void);
void cleanup_server(void);

// 소켓 관리
socket_t create_server_socket(int port);
int bind_server_socket(socket_t server_socket, int port);
int listen_for_connections(socket_t server_socket);
socket_t accept_client_connection(socket_t server_socket);
void close_client_connection(socket_t client_socket);

// 클라이언트 세션 관리
int add_client_session(socket_t client_socket);
void remove_client_session(socket_t client_socket);
ClientSession* find_client_session(socket_t client_socket);
ClientSession* find_client_by_user_id(const char* user_id);
void cleanup_inactive_sessions(void);

// 스레드 처리
#ifdef _WIN32
DWORD WINAPI client_handler_thread(LPVOID arg);
DWORD WINAPI session_cleanup_thread(LPVOID arg);
#else
void* client_handler_thread(void* arg);
void* session_cleanup_thread(void* arg);
#endif

// 메시지 처리
int receive_message(socket_t client_socket, NetworkMessage* msg);
int send_message(socket_t client_socket, NetworkMessage* msg);
void process_client_message(ClientSession* session, NetworkMessage* request, NetworkMessage* response);

// 인증 처리
void handle_login_request(NetworkMessage* request, NetworkMessage* response);
void handle_logout_request(NetworkMessage* request, NetworkMessage* response);
void handle_register_request(const char* user_id, const char* password, NetworkMessage* response);
int authenticate_user_server(const char* user_id, const char* password);
UserInfo* find_user_by_id_server(const char* user_id);
int add_new_user_to_server(const char* user_id, const char* password);
void generate_session_id_server(char* session_id, const char* user_id);
int parse_login_json(const char* json_data, char* user_id, char* password, char* request_type);
int verify_session(const char* session_id, const char* user_id);

// API 연동
int collect_api_data(void);
int collect_elections_only(void);
int collect_candidates_only(void);
int collect_pledges_only(void);
int fetch_election_data(void);
int fetch_candidate_data(const char* election_id);
int fetch_pledge_data(const char* candidate_id);
int call_public_api(const char* url, char* response_buffer, int buffer_size);

// 데이터 처리
void handle_get_elections_request(NetworkMessage* response);
void handle_get_candidates_request(const char* election_id, NetworkMessage* response);
void handle_get_pledges_request(const char* candidate_id, NetworkMessage* response);
void handle_evaluate_pledge_request(const char* user_id, const char* pledge_id, int evaluation_type, NetworkMessage* response);
void handle_get_statistics_request(const char* pledge_id, NetworkMessage* response);

// 평가 시스템
int add_evaluation(const char* user_id, const char* pledge_id, int evaluation_type);
int update_evaluation(const char* user_id, const char* pledge_id, int evaluation_type);
int cancel_evaluation(const char* user_id, const char* pledge_id);
int get_user_evaluation(const char* user_id, const char* pledge_id);
int check_duplicate_evaluation(const char* user_id, const char* pledge_id);
void update_pledge_statistics(const char* pledge_id);
void handle_cancel_evaluation_request(const char* user_id, const char* pledge_id, NetworkMessage* response);
void handle_get_user_evaluation_request(const char* user_id, const char* pledge_id, NetworkMessage* response);
int save_evaluations_to_file(void);

// 데이터 파일 관리
int load_server_data(void);
int save_server_data(void);
int backup_data_files(void);
int load_elections_from_file(ElectionInfo elections[], int max_count);
int load_candidates_from_file(CandidateInfo candidates[], int max_count);
int load_pledges_from_file(PledgeInfo pledges[], int max_count);
int load_evaluations_from_file(void);

// 서버 상태 모니터링
void print_server_status(void);
void print_connected_clients(void);
int get_active_client_count(void);

#endif // SERVER_H 