#ifndef CLIENT_H
#define CLIENT_H

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

// 클라이언트 설정
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define MAX_INPUT_LEN 256
#define MAX_MENU_ITEMS 10
#define BUFFER_SIZE 4096

// 클라이언트 상태
typedef struct {
    socket_t server_socket;
    char user_id[MAX_STRING_LEN];
    char session_id[MAX_STRING_LEN];
    int is_connected;
    int is_logged_in;
} ClientState;

// 메뉴 상태
typedef enum {
    MENU_LOGIN = 0,
    MENU_MAIN,
    MENU_ELECTION_TYPE,
    MENU_ELECTION_DATE,
    MENU_CANDIDATE_LIST,
    MENU_PLEDGE_LIST,
    MENU_PLEDGE_DETAIL,
    MENU_EXIT
} MenuState;

// 클라이언트 초기화 및 종료
int init_client(void);
int connect_to_server(const char* server_ip, int port);
void disconnect_from_server(void);
void cleanup_client(void);

// 네트워크 통신
int send_request_to_server(NetworkMessage* request);
int receive_response_from_server(NetworkMessage* response);
int handle_connection_error(void);

// 사용자 인터페이스
void run_client_ui(void);
int show_login_screen(void);
void show_main_menu(void);
void show_election_selection(void);
void show_candidate_selection(int election_index);
void show_pledge_selection(int candidate_index);
void show_pledge_detail(int pledge_index);
int authenticate_user(const char* user_id, const char* password);
void show_election_type_menu(void);
void show_election_date_menu(void);
void show_candidate_list_menu(void);
void show_pledge_list_menu(void);
void show_pledge_detail_menu(void);

// 로그인/로그아웃 처리
int handle_user_login(void);
int handle_user_logout(void);
int send_login_request(const char* user_id, const char* password);
int send_logout_request(void);

// 회원가입 처리
int show_register_screen(void);
int authenticate_user_local(const char* user_id, const char* password);
int check_user_exists(const char* user_id);
int register_new_user(const char* user_id, const char* password);

// 통계 및 순위 관련
void show_statistics_menu(void);
void show_election_rankings(void);
void show_candidate_rankings(int election_index);

// 데이터 요청 처리
int request_election_list(void);
int request_candidate_list(const char* election_id);
int request_pledge_list(const char* candidate_id);
int request_pledge_statistics(const char* pledge_id);

// 평가 통계 구조체
typedef struct {
    int like_count;
    int dislike_count;
    int total_votes;
    double approval_rate;
} PledgeStatistics;

// 통계 캐시 항목
typedef struct {
    char pledge_id[MAX_STRING_LEN];
    PledgeStatistics stats;
    time_t cache_time;
    int is_valid;
} StatisticsCache;

// 통계 캐시 설정
#define STATISTICS_CACHE_SIZE 100
#define STATISTICS_CACHE_TIMEOUT 30  // 30초

// 평가 처리
int send_pledge_evaluation(const char* pledge_id, int evaluation_type);
void show_evaluation_options(const char* pledge_id);
int send_evaluation_to_server(const char* pledge_id, int evaluation_type);
int get_user_evaluation_from_server(const char* pledge_id);
int cancel_evaluation_on_server(const char* pledge_id);
int get_pledge_statistics_from_server(const char* pledge_id, PledgeStatistics* stats);

// 입력 처리
int get_user_input(char* buffer, int max_length);
int get_menu_choice(int min_choice, int max_choice);
int get_login_credentials(char* user_id, char* password);
int validate_user_input(const char* input, int input_type);

// 화면 표시 함수
void display_election_list(const char* json_data);
void display_candidate_list(const char* json_data);
void display_pledge_list(const char* json_data);
void display_pledge_detail(const char* json_data);
void display_pledge_statistics(const char* json_data);

// 응답 데이터 파싱
int parse_election_response(const char* json_data, ElectionInfo elections[], int max_count);
int parse_candidate_response(const char* json_data, CandidateInfo candidates[], int max_count);
int parse_pledge_response(const char* json_data, PledgeInfo pledges[], int max_count);

// 오류 처리
void handle_server_error(NetworkMessage* response);
void show_error_message(const char* message);
void show_success_message(const char* message);

// 유틸리티 함수
void pause_for_user_input(void);
int confirm_action(const char* message);
void show_loading_message(void);
void hide_loading_message(void);

// 도움말 및 안내
void show_help_screen(void);
void show_about_screen(void);
void show_usage_instructions(void);

#endif // CLIENT_H 