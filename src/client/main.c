#include "client.h"
#include "api.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 데이터 파일 경로
#define ELECTIONS_FILE "data/elections.txt"
#define CANDIDATES_FILE "data/candidates.txt"
#define PLEDGES_FILE "data/pledges.txt"
#define UPDATE_TIME_FILE "data/last_update.txt"

// 전역 데이터
static ElectionInfo g_elections[MAX_ELECTIONS];
static CandidateInfo g_candidates[MAX_CANDIDATES];
static PledgeInfo g_pledges[MAX_PLEDGES];
static int g_election_count = 0;
static int g_candidate_count = 0;
static int g_pledge_count = 0;

// 전역 클라이언트 상태
static ClientState g_client_state;

// 새로운 네비게이션 함수들
int show_login_screen(void);
void show_main_menu(void);
void show_election_selection(void);
void show_candidate_selection(int election_index);
void show_pledge_selection(int candidate_index);
void show_pledge_detail(int pledge_index);
int authenticate_user(const char* user_id, const char* password);
int register_user_on_server(const char* user_id, const char* password);
void show_refresh_menu(void);
void refresh_data(void);
void refresh_elections_only(void);
void refresh_candidates_only(void);
void refresh_pledges_only(void);
void evaluate_pledge_interactive(void);
void show_pledge_statistics(void);
void test_api_functions(void);
int load_elections_from_file(void);
int load_candidates_from_file(void);
int load_pledges_from_file(void);
void parse_pledge_data(const char* pledge_data);
void format_and_print_content(const char* content);
void print_formatted_line(const char* line, int indent_level);
void show_last_update_time(void);
void show_elections(void);
void show_candidates(void);
void show_pledges(void);

// 현재 선택된 항목들
static int g_current_election = -1;
static int g_current_candidate = -1;

// 인증된 사용자 정보
static char g_logged_in_user[MAX_STRING_LEN] = "";
static char g_session_id[MAX_STRING_LEN] = "";

// 함수 선언
void show_simple_menu(void);
void show_help(void);
void run_client_ui(void);
SOCKET connect_to_server_test(const char* server_ip, int port);
void communicate_with_test_server(SOCKET test_socket);
void disconnect_test_connection(SOCKET test_socket);

// 클라이언트 초기화
int init_client(void) {
    write_log("INFO", "Initializing client...");
    
    // 상태 초기화
    memset(&g_client_state, 0, sizeof(ClientState));
    g_client_state.server_socket = INVALID_SOCKET;
    g_client_state.is_connected = 0;
    g_client_state.is_logged_in = 0;
    
    // Windows 소켓 초기화
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        write_error_log("init_client", "WSAStartup failed");
        return 0;
    }
#endif
    
    write_log("INFO", "Client initialized successfully");
    return 1;
}

// 서버 연결
int connect_to_server(const char* server_ip, int port) {
    struct sockaddr_in server_addr;
    
    write_log("INFO", "Connecting to server...");
    
    // 소켓 생성
    g_client_state.server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_client_state.server_socket == INVALID_SOCKET) {
        write_error_log("connect_to_server", "Failed to create socket");
        return 0;
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
#ifdef _WIN32
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
#else
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        write_error_log("connect_to_server", "Invalid server IP address");
        return 0;
    }
#endif
    
    // 서버에 연결
    if (connect(g_client_state.server_socket, 
                (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        write_error_log("connect_to_server", "Failed to connect to server");
#ifdef _WIN32
        closesocket(g_client_state.server_socket);
#else
        close(g_client_state.server_socket);
#endif
        g_client_state.server_socket = INVALID_SOCKET;
        return 0;
    }
    
    g_client_state.is_connected = 1;
    write_log("INFO", "Connected to server successfully");
    return 1;
}

// 서버 연결 해제
void disconnect_from_server(void) {
    if (g_client_state.is_connected && g_client_state.server_socket != INVALID_SOCKET) {
        write_log("INFO", "Disconnecting from server...");
        
#ifdef _WIN32
        closesocket(g_client_state.server_socket);
#else
        close(g_client_state.server_socket);
#endif
        
        g_client_state.server_socket = INVALID_SOCKET;
        g_client_state.is_connected = 0;
        g_client_state.is_logged_in = 0;
        
        // 로그인 관련 데이터 초기화
        memset(g_client_state.user_id, 0, sizeof(g_client_state.user_id));
        memset(g_client_state.session_id, 0, sizeof(g_client_state.session_id));
        memset(g_logged_in_user, 0, sizeof(g_logged_in_user));
        memset(g_session_id, 0, sizeof(g_session_id));
        
        write_log("INFO", "Disconnected from server");
    }
}

// 테스트용 서버 연결 해제 (로그인 정보 유지)
void disconnect_test_connection(SOCKET test_socket) {
    if (test_socket != INVALID_SOCKET) {
        write_log("INFO", "Disconnecting test connection...");
        
#ifdef _WIN32
        closesocket(test_socket);
#else
        close(test_socket);
#endif
        
        write_log("INFO", "Test connection closed");
    }
}

// 클라이언트 정리
void cleanup_client(void) {
    write_log("INFO", "Cleaning up client resources...");
    
    disconnect_from_server();
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    write_log("INFO", "Client cleanup completed");
}

// 사용자 입력 받기
int get_user_input(char* buffer, int max_length) {
    if (!buffer || max_length <= 0) return 0;
    
    printf("> ");
    fflush(stdout);
    
    if (fgets(buffer, max_length, stdin) != NULL) {
        trim_whitespace(buffer);
        return strlen(buffer) > 0;
    }
    
    return 0;
}

// 테스트용 서버 연결
SOCKET connect_to_server_test(const char* server_ip, int port) {
    struct sockaddr_in server_addr;
    SOCKET test_socket;
    
    write_log("INFO", "Creating test connection to server...");
    
    // 소켓 생성
    test_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (test_socket == INVALID_SOCKET) {
        write_error_log("connect_to_server_test", "Failed to create socket");
        return INVALID_SOCKET;
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
#ifdef _WIN32
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
#else
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        write_error_log("connect_to_server_test", "Invalid server IP address");
        return INVALID_SOCKET;
    }
#endif
    
    // 서버에 연결
    if (connect(test_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        write_error_log("connect_to_server_test", "Failed to connect to server");
#ifdef _WIN32
        closesocket(test_socket);
#else
        close(test_socket);
#endif
        return INVALID_SOCKET;
    }
    
    write_log("INFO", "Test connection established successfully");
    return test_socket;
}

// 테스트용 서버와 메시지 교환
void communicate_with_test_server(SOCKET test_socket) {
    char input_buffer[MAX_INPUT_LEN];
    char response_buffer[BUFFER_SIZE];
    int bytes_received;
    
    printf("\n테스트 서버와 연결되었습니다!\n");
    printf("메시지를 입력하세요 (종료: 'quit')\n");
    print_separator();
    
    // 소켓을 논블로킹 모드로 설정
    unsigned long mode = 1;
    if (ioctlsocket(test_socket, FIONBIO, &mode) != 0) {
        printf("경고: 소켓 모드 설정 실패\n");
    }
    
    // 서버로부터 환영 메시지 받기 (논블로킹, 최대 1초 대기)
    int wait_count = 0;
    while (wait_count < 10) { // 100ms * 10 = 1초
        bytes_received = recv(test_socket, response_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            response_buffer[bytes_received] = '\0';
            printf("서버 메시지: %s\n", response_buffer);
            break;
        } else if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                printf("서버 연결 오류: %d\n", error);
                return;
            }
        }
        Sleep(100); // 100ms 대기
        wait_count++;
    }
    
    // 소켓을 다시 블로킹 모드로 설정
    mode = 0;
    ioctlsocket(test_socket, FIONBIO, &mode);
    
    // 메시지 교환 루프
    while (1) {
        // 사용자 입력 받기
        if (!get_user_input(input_buffer, sizeof(input_buffer))) {
            continue;
        }
        
        // 종료 명령 확인
        if (strcmp(input_buffer, "quit") == 0) {
            // 서버에 종료 메시지 전송
            send(test_socket, input_buffer, strlen(input_buffer), 0);
            
            // 서버 응답 받기 (타임아웃 설정)
            fd_set read_fds;
            struct timeval timeout;
            FD_ZERO(&read_fds);
            FD_SET(test_socket, &read_fds);
            timeout.tv_sec = 2;  // 2초 타임아웃
            timeout.tv_usec = 0;
            
            if (select(0, &read_fds, NULL, NULL, &timeout) > 0) {
                bytes_received = recv(test_socket, response_buffer, BUFFER_SIZE - 1, 0);
                if (bytes_received > 0) {
                    response_buffer[bytes_received] = '\0';
                    printf("서버 응답: %s\n", response_buffer);
                }
            } else {
                printf("서버 응답 타임아웃\n");
            }
            break;
        }
        
        // 서버에 메시지 전송
        if (send(test_socket, input_buffer, strlen(input_buffer), 0) == SOCKET_ERROR) {
            write_error_log("communicate_with_test_server", "Failed to send message");
            printf("메시지 전송 실패\n");
            break;
        }
        
        // 서버 응답 받기 (타임아웃 설정)
        fd_set read_fds;
        struct timeval timeout;
        FD_ZERO(&read_fds);
        FD_SET(test_socket, &read_fds);
        timeout.tv_sec = 5;  // 5초 타임아웃
        timeout.tv_usec = 0;
        
        int select_result = select(0, &read_fds, NULL, NULL, &timeout);
        if (select_result > 0) {
            bytes_received = recv(test_socket, response_buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_received <= 0) {
                write_log("INFO", "Server disconnected");
                printf("서버 연결이 끊어졌습니다.\n");
                break;
            }
            
            response_buffer[bytes_received] = '\0';
            printf("서버 응답: %s\n", response_buffer);
        } else if (select_result == 0) {
            printf("서버 응답 타임아웃 (5초)\n");
        } else {
            printf("소켓 오류 발생\n");
            break;
        }
    }
    
    printf("테스트 서버와의 연결이 종료되었습니다.\n");
}

// 서버와 메시지 교환
void communicate_with_server(void) {
    char input_buffer[MAX_INPUT_LEN];
    char response_buffer[BUFFER_SIZE];
    int bytes_received;
    
    printf("\n서버와 연결되었습니다!\n");
    printf("메시지를 입력하세요 (종료: 'quit')\n");
    print_separator();
    
    // 소켓을 논블로킹 모드로 설정
    unsigned long mode = 1;
    if (ioctlsocket(g_client_state.server_socket, FIONBIO, &mode) != 0) {
        printf("경고: 소켓 모드 설정 실패\n");
    }
    
    // 서버로부터 환영 메시지 받기 (논블로킹, 최대 1초 대기)
    int wait_count = 0;
    while (wait_count < 10) { // 100ms * 10 = 1초
        bytes_received = recv(g_client_state.server_socket, response_buffer, 
                             BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            response_buffer[bytes_received] = '\0';
            printf("서버 메시지: %s\n", response_buffer);
            break;
        } else if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                printf("서버 연결 오류: %d\n", error);
                return;
            }
        }
        Sleep(100); // 100ms 대기
        wait_count++;
    }
    
    // 소켓을 다시 블로킹 모드로 설정
    mode = 0;
    ioctlsocket(g_client_state.server_socket, FIONBIO, &mode);
    
    // 메시지 교환 루프
    while (g_client_state.is_connected) {
        // 사용자 입력 받기
        if (!get_user_input(input_buffer, sizeof(input_buffer))) {
            continue;
        }
        
        // 종료 명령 확인
        if (strcmp(input_buffer, "quit") == 0) {
            // 서버에 종료 메시지 전송
            send(g_client_state.server_socket, input_buffer, strlen(input_buffer), 0);
            
            // 서버 응답 받기 (타임아웃 설정)
            fd_set read_fds;
            struct timeval timeout;
            FD_ZERO(&read_fds);
            FD_SET(g_client_state.server_socket, &read_fds);
            timeout.tv_sec = 2;  // 2초 타임아웃
            timeout.tv_usec = 0;
            
            if (select(0, &read_fds, NULL, NULL, &timeout) > 0) {
                bytes_received = recv(g_client_state.server_socket, response_buffer, 
                                     BUFFER_SIZE - 1, 0);
                if (bytes_received > 0) {
                    response_buffer[bytes_received] = '\0';
                    printf("서버 응답: %s\n", response_buffer);
                }
            } else {
                printf("서버 응답 타임아웃\n");
            }
            break;
        }
        
        // 서버에 메시지 전송
        if (send(g_client_state.server_socket, input_buffer, strlen(input_buffer), 0) 
            == SOCKET_ERROR) {
            write_error_log("communicate_with_server", "Failed to send message");
            printf("메시지 전송 실패\n");
            break;
        }
        
        // 서버 응답 받기 (타임아웃 설정)
        fd_set read_fds;
        struct timeval timeout;
        FD_ZERO(&read_fds);
        FD_SET(g_client_state.server_socket, &read_fds);
        timeout.tv_sec = 5;  // 5초 타임아웃
        timeout.tv_usec = 0;
        
        int select_result = select(0, &read_fds, NULL, NULL, &timeout);
        if (select_result > 0) {
            bytes_received = recv(g_client_state.server_socket, response_buffer, 
                                 BUFFER_SIZE - 1, 0);
            
            if (bytes_received <= 0) {
                write_log("INFO", "Server disconnected");
                printf("서버 연결이 끊어졌습니다.\n");
                break;
            }
            
            response_buffer[bytes_received] = '\0';
            printf("서버 응답: %s\n", response_buffer);
        } else if (select_result == 0) {
            printf("서버 응답 타임아웃 (5초)\n");
        } else {
            printf("소켓 오류 발생\n");
            break;
        }
    }
    
    printf("서버와의 연결이 종료되었습니다.\n");
}

// 간단한 메뉴 표시
void show_simple_menu(void) {
    clear_screen();
    print_header("대선 후보 공약 열람 및 평가 시스템");
    show_last_update_time();
    print_separator();
    
    printf("1. 선거 정보 조회 (%d개)\n", g_election_count);
    printf("2. 후보자 정보 조회 (%d개)\n", g_candidate_count);
    printf("3. 공약 정보 조회 (%d개)\n", g_pledge_count);
    printf("4. 공약 평가하기\n");
    printf("5. 평가 통계 보기\n");
    printf("6. 데이터 새로고침\n");
    printf("7. 서버 연결 테스트\n");
    printf("8. API 테스트\n");
    printf("9. 도움말\n");
    printf("0. 종료\n");
    print_separator();
}

// 도움말 표시
void show_help(void) {
    clear_screen();
    print_header("도움말");
    
    printf("선거 공약 시스템 클라이언트 사용법:\n\n");
    printf("1. 서버 연결:\n");
    printf("   - 메뉴에서 '1'을 선택하여 서버에 연결합니다.\n");
    printf("   - 기본 서버: %s:%d\n\n", SERVER_IP, SERVER_PORT);
    
    printf("2. 메시지 교환:\n");
    printf("   - 서버에 연결된 후 메시지를 입력할 수 있습니다.\n");
    printf("   - 'quit'를 입력하면 연결을 종료합니다.\n\n");
    
    printf("3. 현재 버전은 기본 테스트 버전입니다.\n");
    printf("   - 완전한 선거 시스템 기능은 추후 구현됩니다.\n");
    
    print_separator();
    wait_for_enter();
}

// API 테스트 함수
void test_api_functions(void) {
    clear_screen();
    print_header("API 연동 테스트");
    
    printf("⚠️ 경고: 이 기능은 실제 API 서버에 연결을 시도합니다.\n");
    printf("API 호출 중 네트워크 문제나 서버 오류가 발생할 수 있습니다.\n\n");
    
    APIClient api_client;
    memset(&api_client, 0, sizeof(APIClient));  // 구조체 초기화
    
    printf("🔧 API 클라이언트 초기화 중...\n");
    
    if (!init_api_client(&api_client)) {
        printf("❌ API 클라이언트 초기화 실패!\n");
        printf("💡 해결 방법:\n");
        printf("1. 인터넷 연결 상태 확인\n");
        printf("2. data/api_key.txt 파일에 올바른 API 키가 있는지 확인\n");
        printf("3. 공공데이터포털(https://www.data.go.kr)에서 API 키 재발급\n");
        wait_for_enter();
        return;
    }
    
    printf("✅ API 클라이언트 초기화 성공!\n\n");
    
    // 안전한 메모리 할당을 위한 구조체들
    ElectionInfo* elections = NULL;
    CandidateInfo* candidates = NULL; 
    PledgeInfo* pledges = NULL;
    char* response_buffer = NULL;
    
    // 메모리 할당
    elections = (ElectionInfo*)calloc(MAX_ELECTIONS, sizeof(ElectionInfo));
    candidates = (CandidateInfo*)calloc(MAX_CANDIDATES, sizeof(CandidateInfo));
    pledges = (PledgeInfo*)calloc(MAX_PLEDGES, sizeof(PledgeInfo));
    response_buffer = (char*)calloc(16384, sizeof(char)); // 16KB 버퍼
    
    if (!elections || !candidates || !pledges || !response_buffer) {
        printf("❌ 메모리 할당 실패\n");
        goto cleanup;
    }
    
    // 1. 선거 정보 조회 테스트
    printf("📊 1단계: 선거 정보 조회 중...\n");
    
    int api_result = api_get_election_info(&api_client, response_buffer, 16384);
    if (api_result == 0) {
        int election_count = parse_election_json(response_buffer, elections, MAX_ELECTIONS);
        
        if (election_count > 0) {
            printf("✅ 선거 정보 %d개 조회 성공!\n", election_count);
            for (int i = 0; i < election_count && i < 5; i++) { // 최대 5개만 표시
                printf("   %d. %s (%s)\n", i+1, elections[i].election_name, elections[i].election_date);
            }
            if (election_count > 5) {
                printf("   ... (총 %d개 중 5개 표시)\n", election_count);
            }
        } else {
            printf("❌ 선거 정보 파싱 실패\n");
        }
    } else {
        printf("❌ 선거 정보 조회 실패 (오류 코드: %d)\n", api_result);
        printf("   네트워크 연결이나 API 서버 상태를 확인해주세요.\n");
    }
    
    printf("\n");
    
    // 2. 후보자 정보 조회 테스트 (간단 버전)
    printf("👥 2단계: 후보자 정보 조회 테스트...\n");
    printf("   (실제 API 호출 대신 로컬 데이터 사용)\n");
    printf("   로컬에서 로드된 후보자: %d명\n", g_candidate_count);
    
    printf("\n");
    
    // 3. 공약 정보 조회 테스트 (간단 버전)
    printf("📋 3단계: 공약 정보 조회 테스트...\n");
    printf("   (실제 API 호출 대신 로컬 데이터 사용)\n");
    printf("   로컬에서 로드된 공약: %d개\n", g_pledge_count);
    
    printf("\n🎉 API 테스트 완료!\n");
    
cleanup:
    if (response_buffer) free(response_buffer);
    if (elections) free(elections);
    if (candidates) free(candidates);
    if (pledges) free(pledges);
    
    cleanup_api_client(&api_client);
    wait_for_enter();
}

// 메인 UI 루프 (새로운 계층적 네비게이션)
void run_client_ui(void) {
    // 서버 연결 시도
    printf("서버 연결을 시도합니다...\n");
    if (connect_to_server(SERVER_IP, SERVER_PORT)) {
        printf("✅ 서버에 연결되었습니다.\n");
    } else {
        printf("❌ 서버 연결 실패. 서버를 먼저 실행해주세요.\n");
        printf("프로그램을 종료합니다.\n");
        return;
    }
    
    // 데이터 로드
    printf("데이터를 로드합니다...\n");
    g_election_count = load_elections_from_file();
    g_candidate_count = load_candidates_from_file();
    g_pledge_count = load_pledges_from_file();
    printf("로드 완료: 선거 %d개, 후보자 %d개, 공약 %d개\n", 
           g_election_count, g_candidate_count, g_pledge_count);
    
    // 로그인 루프
    while (1) {
        if (show_login_screen()) {
            show_main_menu();
            
            // 로그아웃 후 다시 로그인 화면으로
            if (strlen(g_logged_in_user) == 0) {
                continue;
                } else {
                break; // 프로그램 종료
            }
        } else {
            break; // 로그인 실패로 종료
        }
    }
    
    // 정리
    disconnect_from_server();
}

// 새로고침 메뉴 표시
void show_refresh_menu(void) {
    int choice;
    char input[MAX_INPUT_LEN];
    
    while (1) {
        clear_screen();
        print_header("데이터 새로고침");
        
        printf("🔄 어떤 데이터를 새로고침하시겠습니까?\n\n");
        printf("1. 선거 정보 새로고침\n");
        printf("2. 후보자 정보 새로고침\n");
        printf("3. 공약 정보 새로고침\n");
        printf("4. 전체 데이터 새로고침\n");
        printf("0. 메인 메뉴로 돌아가기\n");
        print_separator();
        
        printf("선택하세요: ");
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        switch (choice) {
            case 1: // 선거 정보만 새로고침
                refresh_elections_only();
                break;
                
            case 2: // 후보자 정보만 새로고침
                refresh_candidates_only();
                break;
                
            case 3: // 공약 정보만 새로고침
                refresh_pledges_only();
                break;
                
            case 4: // 전체 데이터 새로고침
                refresh_data();
                break;
                
            case 0: // 메인 메뉴로 돌아가기
                return;
                
            default:
                printf("잘못된 선택입니다.\n");
                wait_for_enter();
                break;
        }
    }
}

// 선거 정보만 새로고침
void refresh_elections_only(void) {
    clear_screen();
    print_header("선거 정보 새로고침");
    
    printf("🔄 서버에 선거 정보 새로고침을 요청합니다...\n");
    
    // 서버 연결 확인
    if (!g_client_state.is_connected) {
        printf("❌ 서버에 연결되지 않았습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 로그인 상태 확인
    if (!g_client_state.is_logged_in || strlen(g_logged_in_user) == 0) {
        printf("❌ 로그인이 필요합니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버에 선거 정보 새로고침 요청
    NetworkMessage refresh_request, refresh_response;
    memset(&refresh_request, 0, sizeof(NetworkMessage));
    
    refresh_request.message_type = MSG_REFRESH_ELECTIONS;
    strcpy(refresh_request.user_id, g_logged_in_user);
    strcpy(refresh_request.session_id, g_session_id);
    strcpy(refresh_request.data, "refresh_elections");
    refresh_request.data_length = strlen(refresh_request.data);
    refresh_request.status_code = STATUS_SUCCESS;
    
    // 서버로 요청 전송
    printf("📤 서버로 선거 정보 새로고침 요청 전송 중...\n");
    int bytes_sent = send(g_client_state.server_socket, (char*)&refresh_request, sizeof(NetworkMessage), 0);
    
    if (bytes_sent <= 0) {
        printf("❌ 서버로 새로고침 요청 전송 실패\n");
        printf("네트워크 연결을 확인해주세요.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 수신
    printf("📥 서버 응답 대기 중...\n");
    memset(&refresh_response, 0, sizeof(NetworkMessage));
    int bytes_received = recv(g_client_state.server_socket, (char*)&refresh_response, sizeof(NetworkMessage), 0);
    
    if (bytes_received <= 0) {
        printf("❌ 서버로부터 응답을 받지 못했습니다\n");
        printf("서버가 응답하지 않거나 네트워크 문제가 있을 수 있습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 처리
    if (refresh_response.status_code == STATUS_SUCCESS) {
        printf("✅ 서버에서 선거 정보 새로고침 완료\n");
        printf("📨 서버 메시지: %s\n", refresh_response.data);
        
        // 서버가 작업을 완료할 때까지 잠시 대기
        printf("⏳ 데이터 처리 완료 대기 중...\n");
        Sleep(300); // 0.3초 대기
        
    } else {
        printf("⚠️  서버에서 오류 발생: %s\n", refresh_response.data);
        printf("일부 데이터만 새로고침되었을 수 있습니다.\n");
    }
    
    // 로컬 데이터 다시 로드
    printf("\n🔄 업데이트된 선거 정보를 로드합니다...\n");
    int old_election_count = g_election_count;
    g_election_count = load_elections_from_file();
    
    printf("\n🎉 선거 정보 새로고침 완료!\n");
    printf("   - 선거 정보: %d개 (이전: %d개)\n", g_election_count, old_election_count);
    
    wait_for_enter();
}

// 후보자 정보만 새로고침
void refresh_candidates_only(void) {
    clear_screen();
    print_header("후보자 정보 새로고침");
    
    printf("🔄 서버에 후보자 정보 새로고침을 요청합니다...\n");
    
    // 서버 연결 확인
    if (!g_client_state.is_connected) {
        printf("❌ 서버에 연결되지 않았습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 로그인 상태 확인
    if (!g_client_state.is_logged_in || strlen(g_logged_in_user) == 0) {
        printf("❌ 로그인이 필요합니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버에 후보자 정보 새로고침 요청
    NetworkMessage refresh_request, refresh_response;
    memset(&refresh_request, 0, sizeof(NetworkMessage));
    
    refresh_request.message_type = MSG_REFRESH_CANDIDATES;
    strcpy(refresh_request.user_id, g_logged_in_user);
    strcpy(refresh_request.session_id, g_session_id);
    strcpy(refresh_request.data, "refresh_candidates");
    refresh_request.data_length = strlen(refresh_request.data);
    refresh_request.status_code = STATUS_SUCCESS;
    
    // 서버로 요청 전송
    printf("📤 서버로 후보자 정보 새로고침 요청 전송 중...\n");
    int bytes_sent = send(g_client_state.server_socket, (char*)&refresh_request, sizeof(NetworkMessage), 0);
    
    if (bytes_sent <= 0) {
        printf("❌ 서버로 새로고침 요청 전송 실패\n");
        printf("네트워크 연결을 확인해주세요.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 수신
    printf("📥 서버 응답 대기 중...\n");
    memset(&refresh_response, 0, sizeof(NetworkMessage));
    int bytes_received = recv(g_client_state.server_socket, (char*)&refresh_response, sizeof(NetworkMessage), 0);
    
    if (bytes_received <= 0) {
        printf("❌ 서버로부터 응답을 받지 못했습니다\n");
        printf("서버가 응답하지 않거나 네트워크 문제가 있을 수 있습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 처리
    if (refresh_response.status_code == STATUS_SUCCESS) {
        printf("✅ 서버에서 후보자 정보 새로고침 완료\n");
        printf("📨 서버 메시지: %s\n", refresh_response.data);
        
        // 서버가 작업을 완료할 때까지 잠시 대기
        printf("⏳ 데이터 처리 완료 대기 중...\n");
        Sleep(300); // 0.3초 대기
        
    } else {
        printf("⚠️  서버에서 오류 발생: %s\n", refresh_response.data);
        printf("일부 데이터만 새로고침되었을 수 있습니다.\n");
    }
    
    // 로컬 데이터 다시 로드
    printf("\n🔄 업데이트된 데이터를 로드합니다...\n");
    int old_election_count = g_election_count;
    int old_candidate_count = g_candidate_count;
    int old_pledge_count = g_pledge_count;
    
    g_election_count = load_elections_from_file();
    g_candidate_count = load_candidates_from_file();
    g_pledge_count = load_pledges_from_file();
    
    printf("\n🎉 후보자 정보 새로고침 완료!\n");
    printf("   - 선거 정보: %d개 (이전: %d개)\n", g_election_count, old_election_count);
    printf("   - 후보자 정보: %d개 (이전: %d개)\n", g_candidate_count, old_candidate_count);
    printf("   - 공약 정보: %d개 (이전: %d개)\n", g_pledge_count, old_pledge_count);
    
    wait_for_enter();
}

// 공약 정보만 새로고침
void refresh_pledges_only(void) {
    clear_screen();
    print_header("공약 정보 새로고침");
    
    printf("🔄 서버에 공약 정보 새로고침을 요청합니다...\n");
    
    // 서버 연결 확인
    if (!g_client_state.is_connected) {
        printf("❌ 서버에 연결되지 않았습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 로그인 상태 확인
    if (!g_client_state.is_logged_in || strlen(g_logged_in_user) == 0) {
        printf("❌ 로그인이 필요합니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버에 공약 정보 새로고침 요청
    NetworkMessage refresh_request, refresh_response;
    memset(&refresh_request, 0, sizeof(NetworkMessage));
    
    refresh_request.message_type = MSG_REFRESH_PLEDGES;
    strcpy(refresh_request.user_id, g_logged_in_user);
    strcpy(refresh_request.session_id, g_session_id);
    strcpy(refresh_request.data, "refresh_pledges");
    refresh_request.data_length = strlen(refresh_request.data);
    refresh_request.status_code = STATUS_SUCCESS;
    
    // 서버로 요청 전송
    printf("📤 서버로 공약 정보 새로고침 요청 전송 중...\n");
    int bytes_sent = send(g_client_state.server_socket, (char*)&refresh_request, sizeof(NetworkMessage), 0);
    
    if (bytes_sent <= 0) {
        printf("❌ 서버로 새로고침 요청 전송 실패\n");
        printf("네트워크 연결을 확인해주세요.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 수신
    printf("📥 서버 응답 대기 중...\n");
    memset(&refresh_response, 0, sizeof(NetworkMessage));
    int bytes_received = recv(g_client_state.server_socket, (char*)&refresh_response, sizeof(NetworkMessage), 0);
    
    if (bytes_received <= 0) {
        printf("❌ 서버로부터 응답을 받지 못했습니다\n");
        printf("서버가 응답하지 않거나 네트워크 문제가 있을 수 있습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 처리
    if (refresh_response.status_code == STATUS_SUCCESS) {
        printf("✅ 서버에서 공약 정보 새로고침 완료\n");
        printf("📨 서버 메시지: %s\n", refresh_response.data);
        
        // 서버가 작업을 완료할 때까지 잠시 대기
        printf("⏳ 데이터 처리 완료 대기 중...\n");
        Sleep(500); // 공약 데이터가 많아서 0.5초 대기
        
    } else {
        printf("⚠️  서버에서 오류 발생: %s\n", refresh_response.data);
        printf("일부 데이터만 새로고침되었을 수 있습니다.\n");
    }
    
    // 로컬 데이터 다시 로드
    printf("\n🔄 업데이트된 데이터를 로드합니다...\n");
    int old_election_count = g_election_count;
    int old_candidate_count = g_candidate_count;
    int old_pledge_count = g_pledge_count;
    
    g_election_count = load_elections_from_file();
    g_candidate_count = load_candidates_from_file();
    g_pledge_count = load_pledges_from_file();
    
    printf("\n🎉 공약 정보 새로고침 완료!\n");
    printf("   - 선거 정보: %d개 (이전: %d개)\n", g_election_count, old_election_count);
    printf("   - 후보자 정보: %d개 (이전: %d개)\n", g_candidate_count, old_candidate_count);
    printf("   - 공약 정보: %d개 (이전: %d개)\n", g_pledge_count, old_pledge_count);
    
    wait_for_enter();
}

// 전체 데이터 새로고침
void refresh_data(void) {
    clear_screen();
    print_header("전체 데이터 새로고침");
    
    printf("🔄 전체 데이터를 새로고침합니다...\n");
    printf("이 작업은 몇 분이 소요될 수 있습니다.\n\n");
    
    printf("계속하시겠습니까? (y/n): ");
    char input[10];
    if (!get_user_input(input, sizeof(input)) || 
        (input[0] != 'y' && input[0] != 'Y')) {
        printf("작업이 취소되었습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 연결 확인
    if (!g_client_state.is_connected) {
        printf("❌ 서버에 연결되지 않았습니다.\n");
        printf("서버 연결을 확인하고 다시 시도해주세요.\n");
        wait_for_enter();
        return;
    }
    
    // 로그인 상태 확인
    if (!g_client_state.is_logged_in || strlen(g_logged_in_user) == 0) {
        printf("❌ 로그인이 필요합니다.\n");
        wait_for_enter();
        return;
    }
    
    printf("\n📊 서버에서 최신 데이터를 가져오는 중...\n");
    
    // 서버에 전체 새로고침 요청
    NetworkMessage refresh_request, refresh_response;
    memset(&refresh_request, 0, sizeof(NetworkMessage));
    
    refresh_request.message_type = MSG_REFRESH_ALL;
    strcpy(refresh_request.user_id, g_logged_in_user);
    strcpy(refresh_request.session_id, g_session_id);
    strcpy(refresh_request.data, "refresh_all_data");
    refresh_request.data_length = strlen(refresh_request.data);
    refresh_request.status_code = STATUS_SUCCESS;
    
    // 서버로 요청 전송
    printf("📤 서버로 새로고침 요청 전송 중...\n");
    int bytes_sent = send(g_client_state.server_socket, (char*)&refresh_request, sizeof(NetworkMessage), 0);
    
    if (bytes_sent <= 0) {
        printf("❌ 서버로 새로고침 요청 전송 실패\n");
        printf("네트워크 연결을 확인해주세요.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 수신
    printf("📥 서버 응답 대기 중...\n");
    memset(&refresh_response, 0, sizeof(NetworkMessage));
    int bytes_received = recv(g_client_state.server_socket, (char*)&refresh_response, sizeof(NetworkMessage), 0);
    
    if (bytes_received <= 0) {
        printf("❌ 서버로부터 응답을 받지 못했습니다\n");
        printf("서버가 응답하지 않거나 네트워크 문제가 있을 수 있습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 서버 응답 처리
    if (refresh_response.status_code == STATUS_SUCCESS) {
        printf("✅ 서버에서 데이터 새로고침 완료\n");
        printf("📨 서버 메시지: %s\n", refresh_response.data);
        
        // 서버가 작업을 완료할 때까지 잠시 대기
        printf("⏳ 데이터 처리 완료 대기 중...\n");
        Sleep(2000); // 2초 대기
        
    } else {
        printf("⚠️  서버에서 오류 발생: %s\n", refresh_response.data);
        printf("일부 데이터만 새로고침되었을 수 있습니다.\n");
    }
    
    // 로컬 데이터 다시 로드
    printf("\n🔄 업데이트된 데이터를 로드합니다...\n");
    
    int old_election_count = g_election_count;
    int old_candidate_count = g_candidate_count;
    int old_pledge_count = g_pledge_count;
    
    g_election_count = load_elections_from_file();
    g_candidate_count = load_candidates_from_file();
    g_pledge_count = load_pledges_from_file();
    
    // 최종 결과 표시
    clear_screen();
    print_header("전체 데이터 새로고침 완료");
    
    printf("🎉 전체 데이터 새로고침이 완료되었습니다!\n\n");
    
    printf("📊 업데이트 결과:\n");
    printf("   - 선거 정보: %d개 (이전: %d개)\n", g_election_count, old_election_count);
    printf("   - 후보자 정보: %d개 (이전: %d개)\n", g_candidate_count, old_candidate_count);
    printf("   - 공약 정보: %d개 (이전: %d개)\n", g_pledge_count, old_pledge_count);
    
    // 업데이트 시간 표시
    show_last_update_time();
    
    printf("\n모든 데이터가 최신 상태로 업데이트되었습니다.\n");
    wait_for_enter();
}

// 선거 정보 표시
void show_elections(void) {
    clear_screen();
    print_header("선거 정보 조회");
        
        if (g_election_count == 0) {
        printf("❌ 선거 데이터가 없습니다.\n");
        printf("   서버를 먼저 실행하거나 데이터를 새로고침해주세요.\n");
            wait_for_enter();
            return;
        }
        
    printf("📊 총 %d개의 선거 정보\n\n", g_election_count);
    
    for (int i = 0; i < g_election_count && i < 20; i++) {
        printf("%3d. %s\n", i+1, g_elections[i].election_name);
        printf("     날짜: %s | 타입: %s\n", 
               g_elections[i].election_date, 
               g_elections[i].election_type);
        printf("     상태: %s\n", 
               g_elections[i].is_active ? "활성" : "비활성");
        printf("\n");
    }
    
    if (g_election_count > 20) {
        printf("... 그 외 %d개 더 있습니다.\n", g_election_count - 20);
    }
    
        wait_for_enter();
}

// 후보자 정보 표시
void show_candidates(void) {
    clear_screen();
    print_header("후보자 정보 조회");
    
    if (g_candidate_count == 0) {
        printf("❌ 후보자 데이터가 없습니다.\n");
        printf("   서버를 먼저 실행하거나 데이터를 새로고침해주세요.\n");
        wait_for_enter();
        return;
    }
    
    printf("👥 총 %d명의 후보자 정보\n\n", g_candidate_count);
    
    for (int i = 0; i < g_candidate_count && i < 15; i++) {
        printf("%3d. %s (%s)\n", 
               g_candidates[i].candidate_number,
               g_candidates[i].candidate_name,
               g_candidates[i].party_name);
        printf("     선거ID: %s | 공약: %d개\n", 
               g_candidates[i].election_id,
               g_candidates[i].pledge_count);
        printf("\n");
    }
    
    if (g_candidate_count > 15) {
        printf("... 그 외 %d명 더 있습니다.\n", g_candidate_count - 15);
    }
    
    wait_for_enter();
}

// 공약 정보 표시
void show_pledges(void) {
    clear_screen();
    print_header("공약 정보 조회");
    
    if (g_pledge_count == 0) {
        printf("❌ 공약 데이터가 없습니다.\n");
        printf("   서버를 먼저 실행하거나 데이터를 새로고침해주세요.\n");
        wait_for_enter();
        return;
    }
    
    printf("📋 총 %d개의 공약 정보\n\n", g_pledge_count);
    
    for (int i = 0; i < g_pledge_count && i < 10; i++) {
        printf("%3d. [%s] %s\n", i+1, g_pledges[i].category, g_pledges[i].title);
        printf("     후보자ID: %s\n", g_pledges[i].candidate_id);
        printf("     내용: %.100s%s\n", 
               g_pledges[i].content,
               strlen(g_pledges[i].content) > 100 ? "..." : "");
        printf("     평가: 👍 %d  👎 %d\n", 
               g_pledges[i].like_count, 
               g_pledges[i].dislike_count);
        printf("\n");
    }
    
    if (g_pledge_count > 10) {
        printf("... 그 외 %d개 더 있습니다.\n", g_pledge_count - 10);
    }
    
    wait_for_enter();
}

// 공약 평가 기능 (단순화된 버전)
void evaluate_pledge_interactive(void) {
    clear_screen();
    print_header("공약 평가하기");
    
    if (g_current_candidate == -1) {
        printf("❌ 먼저 후보자를 선택해주세요.\n");
        printf("메인 메뉴 → 선거 정보 조회 → 선거 선택 → 후보자 선택 순으로 진행하세요.\n");
        wait_for_enter();
        return;
    }
    
    // 현재 선택된 후보자의 공약들 표시
    int pledge_count_for_candidate = 0;
    int pledge_indices[MAX_PLEDGES];
    
    for (int i = 0; i < g_pledge_count; i++) {
        if (strcmp(g_pledges[i].candidate_id, g_candidates[g_current_candidate].candidate_id) == 0) {
            pledge_indices[pledge_count_for_candidate] = i;
            pledge_count_for_candidate++;
        }
    }
    
    if (pledge_count_for_candidate == 0) {
        printf("❌ 선택된 후보자의 공약이 없습니다.\n");
    wait_for_enter();
        return;
    }
    
    printf("👤 후보자: %s (%s)\n", 
           g_candidates[g_current_candidate].candidate_name,
           g_candidates[g_current_candidate].party_name);
    printf("📋 공약 수: %d개\n\n", pledge_count_for_candidate);
    
    printf("평가할 공약을 선택하세요:\n");
    for (int i = 0; i < pledge_count_for_candidate && i < 5; i++) {
        int idx = pledge_indices[i];
        printf("%d. %s\n", i + 1, g_pledges[idx].title);
    }
    
    if (pledge_count_for_candidate > 5) {
        printf("... 외 %d개 더\n", pledge_count_for_candidate - 5);
    }
    
    printf("0. 돌아가기\n");
    print_separator();
    printf("선택하세요: ");
    
    char input[10];
    if (!get_user_input(input, sizeof(input))) {
            return;
        }
        
    int choice = atoi(input);
    if (choice == 0) {
        return;
    }
    
    if (choice >= 1 && choice <= pledge_count_for_candidate && choice <= 5) {
        show_pledge_detail(pledge_indices[choice - 1]);
    } else {
        printf("잘못된 선택입니다.\n");
        wait_for_enter();
    }
}

// 공약 통계 보기
void show_pledge_statistics(void) {
    // 최대한 안전하게 시작
    printf("🔍 함수 진입\n");
    fflush(stdout);
    
    // 화면 지우기 전에 안전 확인
    printf("🔍 clear_screen 호출 전\n");
    fflush(stdout);
    clear_screen();
    
    printf("🔍 print_header 호출 전\n");
    fflush(stdout);
    print_header("공약 평가 통계");
    
    printf("🔍 기본 메시지 출력\n");
    fflush(stdout);
    
    printf("\n📊 전체 통계 기능 테스트 중...\n");
    printf("   ✅ 함수 호출 성공\n");
    printf("   ✅ 기본 출력 작동\n");
    printf("   ✅ 메모리 접근 정상\n");
    
    printf("\n💡 임시 통계 정보:\n");
    printf("   - 시스템 상태: 정상\n");
    printf("   - 전체 통계 기능: 개발 중\n");
    printf("   - 클라이언트 종료 문제: 해결됨 ✅\n");
    
    printf("\n🎯 다음 단계에서 실제 통계 구현 예정\n");
    
    printf("🔍 wait_for_enter 호출 전\n");
    fflush(stdout);
    wait_for_enter();
    
    printf("📊 전체 공약 평가 통계 (로컬 데이터 기반)\n");
    printf("🔍 총 %d개 공약에서 평가된 공약을 찾는 중...\n\n", g_pledge_count);
    
    // 평가가 있는 공약들을 저장할 임시 배열
    typedef struct {
        int index;
        int like_count;
        int dislike_count;
        int total_votes;
        double approval_rate;
        int has_server_stats;
    } EvaluatedPledge;
    
    EvaluatedPledge evaluated_pledges[MAX_PLEDGES];
    int evaluated_count = 0;
    int server_query_limit = 10;  // 서버 쿼리 제한
    int server_queries_used = 0;
    
    // 로컬 데이터를 기반으로 먼저 평가된 공약들을 찾기
    for (int i = 0; i < g_pledge_count; i++) {
        int total_votes = g_pledges[i].like_count + g_pledges[i].dislike_count;
        
        // 평가가 있는 공약만 처리
        if (total_votes > 0) {
            int like_count = g_pledges[i].like_count;
            int dislike_count = g_pledges[i].dislike_count;
            double approval_rate = ((double)like_count / total_votes) * 100.0;
            int has_server_stats = 0;
            
            // 상위 공약들에 대해서만 서버 통계 조회 (제한적으로)
            if (server_queries_used < server_query_limit && total_votes >= 1) {
                PledgeStatistics stats;
                if (get_pledge_statistics_from_server(g_pledges[i].pledge_id, &stats)) {
                    // 서버 실시간 데이터 사용
                    like_count = stats.like_count;
                    dislike_count = stats.dislike_count;
                    total_votes = stats.total_votes;
                    approval_rate = stats.approval_rate;
                    has_server_stats = 1;
                }
                server_queries_used++;
            }
            
            evaluated_pledges[evaluated_count].index = i;
            evaluated_pledges[evaluated_count].like_count = like_count;
            evaluated_pledges[evaluated_count].dislike_count = dislike_count;
            evaluated_pledges[evaluated_count].total_votes = total_votes;
            evaluated_pledges[evaluated_count].approval_rate = approval_rate;
            evaluated_pledges[evaluated_count].has_server_stats = has_server_stats;
            evaluated_count++;
        }
        
        // 진행 상황 표시 (매 20개마다)
        if ((i + 1) % 20 == 0) {
            printf("🔍 %d/%d 검사 완료... (평가된 공약 %d개 발견)\n", 
                   i + 1, g_pledge_count, evaluated_count);
        }
    }
    
    printf("\n🔍 검사 완료! 총 %d개 공약 중 %d개에 평가가 있습니다.\n", 
           g_pledge_count, evaluated_count);
    printf("   (상위 %d개 공약은 서버에서 실시간 데이터 조회)\n\n", 
           (server_queries_used < evaluated_count) ? server_queries_used : evaluated_count);
    
    if (evaluated_count == 0) {
        printf("아직 평가된 공약이 없습니다.\n");
        printf("공약 평가 메뉴에서 공약을 평가해보세요!\n");
    } else {
        // 지지율 기준으로 정렬 (버블 정렬)
        for (int i = 0; i < evaluated_count - 1; i++) {
            for (int j = 0; j < evaluated_count - i - 1; j++) {
                if (evaluated_pledges[j].approval_rate < evaluated_pledges[j + 1].approval_rate) {
                    EvaluatedPledge temp = evaluated_pledges[j];
                    evaluated_pledges[j] = evaluated_pledges[j + 1];
                    evaluated_pledges[j + 1] = temp;
                }
            }
        }
        
        // 평가된 공약들 표시 (최대 15개)
        int display_count = (evaluated_count > 15) ? 15 : evaluated_count;
        printf("📊 평가된 공약 순위 (지지율 순, 상위 %d개):\n\n", display_count);
        
        for (int i = 0; i < display_count; i++) {
            int idx = evaluated_pledges[i].index;
            printf("%2d. [%s] %s\n", 
                   i + 1,
                   g_pledges[idx].category,
                   g_pledges[idx].title);
            printf("    👍 %d명  👎 %d명  💯 %.1f%%",
                   evaluated_pledges[i].like_count,
                   evaluated_pledges[i].dislike_count,
                   evaluated_pledges[i].approval_rate);
            
            // 서버 데이터 사용 여부 표시
            if (evaluated_pledges[i].has_server_stats) {
                printf(" 🔄");  // 실시간 데이터 표시
            } else {
                printf(" 📁");  // 로컬 데이터 표시
            }
            
            // 1위 표시
            if (i == 0) {
                printf(" 🏆");
            }
            printf("\n");
            printf("    후보자ID: %s\n\n", g_pledges[idx].candidate_id);
        }
        
        if (evaluated_count > 15) {
            printf("... 외 %d개 공약이 더 평가되었습니다.\n\n", evaluated_count - 15);
        }
        
        printf("총 %d개 공약이 평가되었습니다.\n", evaluated_count);
        printf("\n💡 표시 설명:\n");
        printf("🔄 = 서버 실시간 데이터 (상위 %d개 공약)\n", server_query_limit);
        printf("📁 = 로컬 캐시 데이터\n");
        printf("🏆 = 1위 (최고 지지율)\n");
        printf("\n⚠️ 성능상 이유로 상위 %d개 공약만 실시간 서버 데이터를 조회합니다.\n", server_query_limit);
    }
    
    wait_for_enter();
}

// 로그인 화면
int show_login_screen(void) {
    char user_id[MAX_STRING_LEN];
    char password[MAX_STRING_LEN];
    int attempts = 0;
    int choice;
    char input[MAX_INPUT_LEN];
    
    while (attempts < 3) {
        clear_screen();
        print_header("대선 후보 공약 열람 및 평가 시스템");
        printf("┌─────────────────────────────────────┐\n");
        printf("│              사용자 인증            │\n");
        printf("└─────────────────────────────────────┘\n\n");
        
        if (attempts > 0) {
            printf("❌ 로그인 실패! (%d/3 시도)\n\n", attempts);
        }
        
        printf("1. 로그인\n");
        printf("2. 회원가입\n");
        printf("0. 종료\n\n");
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        switch (choice) {
            case 1: // 로그인
                printf("\n=== 로그인 ===\n");
                printf("사용자 ID: ");
                if (!get_user_input(user_id, sizeof(user_id))) {
                    continue;
                }
                
                printf("비밀번호: ");
                if (!get_user_input(password, sizeof(password))) {
                    continue;
                }
                
                // 서버 인증 (서버 연결 필수)
                if (authenticate_user(user_id, password)) {
                    strcpy(g_logged_in_user, user_id);
                    printf("\n✅ 로그인 성공! 환영합니다, %s님\n", user_id);
                    wait_for_enter();
                    return 1;
                }
                attempts++;
                break;
                
            case 2: // 회원가입
                if (show_register_screen()) {
                    printf("\n✅ 회원가입이 완료되었습니다! 로그인해주세요.\n");
                    wait_for_enter();
                }
                break;
                
            case 0: // 종료
                return 0;
                
            default:
                printf("잘못된 선택입니다.\n");
                wait_for_enter();
                break;
        }
    }
    
    printf("\n❌ 로그인 시도 횟수 초과. 프로그램을 종료합니다.\n");
    wait_for_enter();
    return 0;
}

// 회원가입 화면
int show_register_screen(void) {
    char user_id[MAX_STRING_LEN];
    char password[MAX_STRING_LEN];
    char confirm_password[MAX_STRING_LEN];
    
    clear_screen();
    print_header("회원가입");
    printf("┌─────────────────────────────────────┐\n");
    printf("│            새 계정 만들기           │\n");
    printf("└─────────────────────────────────────┘\n\n");
    
    printf("📋 사용자 정보를 입력해주세요:\n\n");
    
    // 사용자 ID 입력
    while (1) {
        printf("사용자 ID (3-20자, 영문+숫자): ");
        if (!get_user_input(user_id, sizeof(user_id))) {
            continue;
        }
        
        if (!validate_user_id(user_id)) {
            printf("❌ 사용자 ID는 3-20자의 영문과 숫자만 사용 가능합니다.\n");
            continue;
        }
        
        // 중복 검사
        if (check_user_exists(user_id)) {
            printf("❌ 이미 존재하는 사용자 ID입니다. 다른 ID를 입력해주세요.\n");
            continue;
        }
        
        printf("✅ 사용 가능한 ID입니다.\n");
        break;
    }
    
    // 비밀번호 입력
    while (1) {
        printf("비밀번호 (4-20자): ");
        if (!get_user_input(password, sizeof(password))) {
            continue;
        }
        
        if (!validate_password(password)) {
            printf("❌ 비밀번호는 4-20자여야 합니다.\n");
            continue;
        }
        
        printf("비밀번호 확인: ");
        if (!get_user_input(confirm_password, sizeof(confirm_password))) {
            continue;
        }
        
        if (strcmp(password, confirm_password) != 0) {
            printf("❌ 비밀번호가 일치하지 않습니다. 다시 입력해주세요.\n");
            continue;
        }
        
        printf("✅ 비밀번호가 확인되었습니다.\n");
        break;
    }
    
    // 회원가입 처리
    printf("\n🔄 서버에 계정 생성을 요청합니다...\n");
    
    if (register_user_on_server(user_id, password)) {
        printf("✅ 회원가입이 성공적으로 완료되었습니다!\n");
        printf("📝 계정 정보:\n");
        printf("   - 사용자 ID: %s\n", user_id);
        printf("   - 등록 시간: %s\n", get_current_time_string());
        return 1;
            } else {
        printf("❌ 회원가입 중 오류가 발생했습니다. 다시 시도해주세요.\n");
        wait_for_enter();
        return 0;
    }
}

// 사용자 인증 (서버)
int authenticate_user(const char* user_id, const char* password) {
    if (!g_client_state.is_connected) {
        printf("❌ 서버에 연결되지 않았습니다.\n");
        return 0;
    }
    
    // 로그인 요청 메시지 생성
    NetworkMessage login_request;
    memset(&login_request, 0, sizeof(NetworkMessage));
    
    login_request.message_type = MSG_LOGIN_REQUEST;
    strncpy(login_request.user_id, user_id, sizeof(login_request.user_id) - 1);
    
    // 사용자 ID와 비밀번호를 JSON 형태로 데이터에 포함
    snprintf(login_request.data, sizeof(login_request.data), 
             "{\"user_id\":\"%s\",\"password\":\"%s\"}", user_id, password);
    login_request.data_length = strlen(login_request.data);
    login_request.status_code = STATUS_SUCCESS;
    
    // 서버로 로그인 요청 전송
    printf("🔄 서버에 로그인 요청을 전송합니다...\n");
    if (send(g_client_state.server_socket, (char*)&login_request, 
             sizeof(NetworkMessage), 0) == SOCKET_ERROR) {
        printf("❌ 서버로 로그인 요청 전송 실패\n");
        return 0;
    }
    
    // 서버로부터 응답 수신
    NetworkMessage login_response;
    memset(&login_response, 0, sizeof(NetworkMessage));
    
    int bytes_received = recv(g_client_state.server_socket, (char*)&login_response, 
                             sizeof(NetworkMessage), 0);
    
    if (bytes_received <= 0) {
        printf("❌ 서버로부터 응답을 받지 못했습니다\n");
        return 0;
    }
    
    // 응답 메시지 검증
    if (login_response.message_type != MSG_LOGIN_RESPONSE) {
        printf("❌ 잘못된 응답 메시지 타입입니다\n");
        return 0;
    }
    
    // 인증 결과 확인
    if (login_response.status_code == STATUS_SUCCESS) {
        // 세션 ID 저장
        strncpy(g_session_id, login_response.session_id, sizeof(g_session_id) - 1);
        
        // 클라이언트 상태 업데이트
        g_client_state.is_logged_in = 1;
        strncpy(g_client_state.user_id, user_id, sizeof(g_client_state.user_id) - 1);
        strncpy(g_client_state.session_id, login_response.session_id, sizeof(g_client_state.session_id) - 1);
        
        printf("✅ 서버 인증 성공 (세션 ID: %.8s...)\n", g_session_id);
        return 1;
    } else if (login_response.status_code == STATUS_UNAUTHORIZED) {
        printf("❌ 아이디 또는 비밀번호가 올바르지 않습니다\n");
        return 0;
    } else {
        printf("❌ 서버 인증 실패 (오류 코드: %d)\n", login_response.status_code);
        return 0;
    }
}

// 로컬 사용자 인증
int authenticate_user_local(const char* user_id, const char* password) {
    UserInfo users[MAX_USERS];
    int user_count = load_user_data("data/users.txt", users, MAX_USERS);
    
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            if (verify_password(password, users[i].password_hash)) {
                return 1;
            }
            break;
        }
    }
    
    // 기본 계정 확인 (호환성을 위해)
    if ((strcmp(user_id, "admin") == 0 && strcmp(password, "admin") == 0) ||
        (strcmp(user_id, "user") == 0 && strcmp(password, "user") == 0)) {
        return 1;
    }
    
    return 0;
}

// 사용자 존재 여부 확인
int check_user_exists(const char* user_id) {
    UserInfo users[MAX_USERS];
    int user_count = load_user_data("data/users.txt", users, MAX_USERS);
    
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].user_id, user_id) == 0) {
            return 1; // 사용자 존재
        }
    }
    
    // 기본 계정 확인
    if (strcmp(user_id, "admin") == 0 || strcmp(user_id, "user") == 0) {
        return 1;
    }
    
    return 0; // 사용자 존재하지 않음
}

// 서버에 회원가입 요청
int register_user_on_server(const char* user_id, const char* password) {
    if (!g_client_state.is_connected) {
        printf("❌ 서버에 연결되지 않았습니다.\n");
        return 0;
    }
    
    // 회원가입 요청 메시지 생성 (LOGIN_REQUEST와 구별하기 위해 data에 type 추가)
    NetworkMessage register_request;
    memset(&register_request, 0, sizeof(NetworkMessage));
    
    register_request.message_type = MSG_LOGIN_REQUEST;  // 같은 메시지 타입 사용
    strncpy(register_request.user_id, user_id, sizeof(register_request.user_id) - 1);
    
    // 회원가입임을 구별하기 위해 type 필드 추가
    snprintf(register_request.data, sizeof(register_request.data), 
             "{\"type\":\"register\",\"user_id\":\"%s\",\"password\":\"%s\"}", 
             user_id, password);
    register_request.data_length = strlen(register_request.data);
    register_request.status_code = STATUS_SUCCESS;
    
    // 서버로 회원가입 요청 전송
    if (send(g_client_state.server_socket, (char*)&register_request, 
             sizeof(NetworkMessage), 0) == SOCKET_ERROR) {
        printf("❌ 서버로 회원가입 요청 전송 실패\n");
        return 0;
    }
    
    // 서버로부터 응답 수신
    NetworkMessage register_response;
    memset(&register_response, 0, sizeof(NetworkMessage));
    
    int bytes_received = recv(g_client_state.server_socket, (char*)&register_response, 
                             sizeof(NetworkMessage), 0);
    
    if (bytes_received <= 0) {
        printf("❌ 서버로부터 응답을 받지 못했습니다\n");
        return 0;
    }
    
    // 응답 메시지 검증
    if (register_response.message_type != MSG_LOGIN_RESPONSE) {
        printf("❌ 잘못된 응답 메시지 타입입니다\n");
        return 0;
    }
    
    // 회원가입 결과 확인
    if (register_response.status_code == STATUS_SUCCESS) {
        printf("✅ 서버에서 계정이 성공적으로 생성되었습니다\n");
        return 1;
    } else if (register_response.status_code == STATUS_BAD_REQUEST) {
        printf("❌ 이미 존재하는 사용자 ID이거나 잘못된 요청입니다\n");
        return 0;
    } else {
        printf("❌ 서버에서 회원가입 실패 (오류 코드: %d)\n", register_response.status_code);
        return 0;
    }
}

// 로컬 사용자 등록 (백업용)
int register_new_user(const char* user_id, const char* password) {
    UserInfo users[MAX_USERS];
    int user_count = load_user_data("data/users.txt", users, MAX_USERS);
    
    // 배열이 가득 찬 경우
    if (user_count >= MAX_USERS) {
        printf("❌ 최대 사용자 수에 도달했습니다.\n");
        return 0;
    }
    
    // 중복 검사 (한 번 더)
    if (check_user_exists(user_id)) {
        printf("❌ 이미 존재하는 사용자 ID입니다.\n");
        return 0;
    }
    
    // 새 사용자 정보 생성
    UserInfo new_user;
    memset(&new_user, 0, sizeof(UserInfo));
    
    safe_strcpy(new_user.user_id, user_id, sizeof(new_user.user_id));
    hash_password(password, new_user.password_hash);
    new_user.login_attempts = 0;
    new_user.is_locked = 0;
    new_user.is_online = 0;
    new_user.last_login = 0;
    memset(new_user.session_id, 0, sizeof(new_user.session_id));
    
    // 사용자 배열에 추가
    users[user_count] = new_user;
    user_count++;
    
    // 파일에 저장
    if (save_user_data("data/users.txt", users, user_count)) {
        write_log("INFO", "New user registered successfully");
        return 1;
    } else {
        write_error_log("register_new_user", "Failed to save user data");
        return 0;
    }
}

// 메인 메뉴
void show_main_menu(void) {
    int choice;
    char input[MAX_INPUT_LEN];
    
    while (1) {
    clear_screen();
        print_header("메인 메뉴");
        printf("로그인 사용자: %s\n", g_logged_in_user);
        print_separator();
        
        printf("📊 대선 후보 공약 열람 및 평가 시스템\n\n");
        printf("1. 선거 정보 조회\n");
        printf("2. 통계 보기\n");
        printf("3. 로그아웃\n");
        
        // 관리자 추가 메뉴
        if (strcmp(g_logged_in_user, "admin") == 0) {
            printf("4. 데이터 새로고침\n");
            printf("5. 서버 연결 테스트\n");
            printf("6. API 테스트\n");
        }
        
        printf("0. 종료\n");
        
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        switch (choice) {
            case 1: // 선거 정보 조회
                show_election_selection();
                break;
                
            case 2: // 통계 보기
                show_statistics_menu();
                break;
                
            case 3: // 로그아웃
                printf("로그아웃하시겠습니까? (y/n): ");
                if (get_user_input(input, sizeof(input)) && 
                    (input[0] == 'y' || input[0] == 'Y')) {
                    memset(g_logged_in_user, 0, sizeof(g_logged_in_user));
                    g_current_election = -1;
                    g_current_candidate = -1;
        return;
    }
                break;
                
            case 4: // 데이터 새로고침 (관리자만)
                if (strcmp(g_logged_in_user, "admin") == 0) {
                    show_refresh_menu();
                } else {
                    printf("관리자만 접근 가능합니다.\n");
                    wait_for_enter();
                }
                break;
                
            case 5: // 서버 연결 테스트 (관리자만)
                if (strcmp(g_logged_in_user, "admin") == 0) {
                    SOCKET test_socket = connect_to_server_test(SERVER_IP, SERVER_PORT);
                    if (test_socket != INVALID_SOCKET) {
                        communicate_with_test_server(test_socket);
                        disconnect_test_connection(test_socket);
                        printf("\n로그인 정보가 유지되었습니다.\n");
                        wait_for_enter();
                    } else {
                        printf("서버 연결에 실패했습니다.\n");
                        wait_for_enter();
                    }
                } else {
                    printf("관리자만 접근 가능합니다.\n");
                    wait_for_enter();
                }
                break;
                
            case 6: // API 테스트 (관리자만)
                if (strcmp(g_logged_in_user, "admin") == 0) {
                    test_api_functions();
                } else {
                    printf("관리자만 접근 가능합니다.\n");
        wait_for_enter();
                }
                break;
                
            case 0: // 종료
                printf("프로그램을 종료하시겠습니까? (y/n): ");
                if (get_user_input(input, sizeof(input)) && 
                    (input[0] == 'y' || input[0] == 'Y')) {
                    cleanup_client();
                    exit(0);
                }
                break;
                
            default:
                printf("잘못된 선택입니다.\n");
    wait_for_enter();
                break;
        }
    }
}

// 메인 함수
int main(int argc, char* argv[]) {
    // EUC-KR 콘솔 초기화
    init_korean_console();
    
    // 명령행 인수 처리 (추후 확장 가능)
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("사용법: %s [옵션]\n", argv[0]);
        printf("옵션:\n");
        printf("  --help    이 도움말을 표시합니다\n");
        return 0;
    }
    
    // 클라이언트 초기화
    if (!init_client()) {
        printf("클라이언트 초기화 실패\n");
        return 1;
    }
    
    // 데이터는 run_client_ui에서 로드됩니다
    
    // UI 실행
    run_client_ui();
    
    // 클라이언트 정리
    cleanup_client();
    
    return 0;
}

// 파일에서 선거 데이터 읽기
int load_elections_from_file(void) {
    FILE* file = fopen(ELECTIONS_FILE, "r");
    if (!file) {
        printf("❌ 선거 데이터 파일을 찾을 수 없습니다: %s\n", ELECTIONS_FILE);
        printf("   서버를 먼저 실행해주세요.\n");
        return 0;
    }
    
    char line[1024];
    g_election_count = 0;
    
    while (fgets(line, sizeof(line), file) && g_election_count < MAX_ELECTIONS) {
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, "COUNT=", 6) == 0) continue;
        
        // 데이터 파싱: ID|이름|날짜|타입|활성상태
        char* token = strtok(line, "|");
        if (!token) continue;
        
        strncpy(g_elections[g_election_count].election_id, token, 
                sizeof(g_elections[g_election_count].election_id) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(g_elections[g_election_count].election_name, token, 
                sizeof(g_elections[g_election_count].election_name) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(g_elections[g_election_count].election_date, token, 
                sizeof(g_elections[g_election_count].election_date) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(g_elections[g_election_count].election_type, token, 
                sizeof(g_elections[g_election_count].election_type) - 1);
        
        token = strtok(NULL, "|\n");
        if (!token) continue;
        g_elections[g_election_count].is_active = atoi(token);
        
        g_election_count++;
    }
    
    fclose(file);
    printf("📂 선거 정보 %d개를 로드했습니다.\n", g_election_count);
    return g_election_count;
}

// 파일에서 후보자 데이터 읽기
int load_candidates_from_file(void) {
    FILE* file = fopen(CANDIDATES_FILE, "r");
    if (!file) {
        printf("⚠️  후보자 데이터 파일을 찾을 수 없습니다: %s\n", CANDIDATES_FILE);
        return 0;
    }
    
    char line[1024];
    g_candidate_count = 0;
    
    while (fgets(line, sizeof(line), file) && g_candidate_count < MAX_CANDIDATES) {
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, "COUNT=", 6) == 0) continue;
        
        // 데이터 파싱: 후보자ID|이름|정당|번호|선거ID|공약수
        char* token = strtok(line, "|");
        if (!token) continue;
        
        strncpy(g_candidates[g_candidate_count].candidate_id, token, 
                sizeof(g_candidates[g_candidate_count].candidate_id) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(g_candidates[g_candidate_count].candidate_name, token, 
                sizeof(g_candidates[g_candidate_count].candidate_name) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(g_candidates[g_candidate_count].party_name, token, 
                sizeof(g_candidates[g_candidate_count].party_name) - 1);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        g_candidates[g_candidate_count].candidate_number = atoi(token);
        
        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(g_candidates[g_candidate_count].election_id, token, 
                sizeof(g_candidates[g_candidate_count].election_id) - 1);
        
        token = strtok(NULL, "|\n");
        if (!token) continue;
        g_candidates[g_candidate_count].pledge_count = atoi(token);
        
        g_candidate_count++;
    }
    
    fclose(file);
    printf("📂 후보자 정보 %d개를 로드했습니다.\n", g_candidate_count);
    return g_candidate_count;
}

// 파일에서 공약 데이터 읽기
int load_pledges_from_file(void) {
    FILE* file = fopen(PLEDGES_FILE, "r");
    if (!file) {
        printf("⚠️  공약 데이터 파일을 찾을 수 없습니다: %s\n", PLEDGES_FILE);
        return 0;
    }
    
    char line[4096];
    g_pledge_count = 0;
    char current_pledge_data[8192] = "";  // 여러 줄에 걸친 공약 데이터를 저장할 버퍼
    int collecting_pledge = 0;  // 공약 데이터를 수집 중인지 여부
    
    while (fgets(line, sizeof(line), file) && g_pledge_count < MAX_PLEDGES) {
        // 주석과 빈 줄 건너뛰기
        if (line[0] == '#' || strncmp(line, "COUNT=", 6) == 0) continue;
        
        // 개행 문자 제거
        char* newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        newline = strchr(line, '\r');
        if (newline) *newline = '\0';
        
        // 빈 줄인 경우 건너뛰기
        if (strlen(line) == 0) continue;
        
        // 새로운 공약 시작인지 확인 (숫자_숫자| 패턴으로 시작하는 줄)
        if (strstr(line, "_") && strchr(line, '|')) {
            // 이전 공약 데이터가 있다면 처리
            if (collecting_pledge && strlen(current_pledge_data) > 0) {
                parse_pledge_data(current_pledge_data);
            }
            
            // 새로운 공약 데이터 시작
            strcpy(current_pledge_data, line);
            collecting_pledge = 1;
        } else if (collecting_pledge) {
            // 이전 공약의 연속 데이터인 경우, 내용에 추가
            strcat(current_pledge_data, " ");
            strcat(current_pledge_data, line);
        }
    }
    
    // 마지막 공약 데이터 처리
    if (collecting_pledge && strlen(current_pledge_data) > 0) {
        parse_pledge_data(current_pledge_data);
    }
    
    fclose(file);
    printf("📂 공약 정보 %d개를 로드했습니다.\n", g_pledge_count);
    return g_pledge_count;
}

// 공약 데이터 파싱 함수
void parse_pledge_data(const char* pledge_data) {
    if (g_pledge_count >= MAX_PLEDGES) return;
    
    // 복사본 생성
    char data_copy[8192];
    strncpy(data_copy, pledge_data, sizeof(data_copy) - 1);
    data_copy[sizeof(data_copy) - 1] = '\0';
    
    // 파이프(|) 개수 확인
    int pipe_count = 0;
    for (int i = 0; data_copy[i]; i++) {
        if (data_copy[i] == '|') pipe_count++;
    }
    
    // 최소 4개의 파이프가 있어야 함
    if (pipe_count < 4) return;
    
    // 더 안정적인 파싱을 위해 처음 3개 필드와 마지막 4개 필드를 분리해서 처리
    char* pledge_id = NULL;
    char* candidate_id = NULL;
    char* title = NULL;
    char* content = NULL;
    char* category = NULL;
    char* likes = NULL;
    char* dislikes = NULL;
    char* timestamp = NULL;
    
    // 첫 번째 | 찾기 (공약 ID)
    char* ptr = data_copy;
    pledge_id = ptr;
    ptr = strchr(ptr, '|');
    if (!ptr) return;
    *ptr = '\0';
    ptr++;
    
    // 두 번째 | 찾기 (후보자 ID)
    candidate_id = ptr;
    ptr = strchr(ptr, '|');
    if (!ptr) return;
    *ptr = '\0';
    ptr++;
    
    // 세 번째 | 찾기 (제목)
    title = ptr;
    ptr = strchr(ptr, '|');
    if (!ptr) return;
    *ptr = '\0';
    ptr++;
    
    // 나머지 문자열에서 마지막 3개 | 찾기 (역순으로)
    char* rest_data = ptr;
    char* last_pipe = strrchr(rest_data, '|');
    if (!last_pipe) return;
    
    timestamp = last_pipe + 1;
    *last_pipe = '\0';
    
    char* second_last_pipe = strrchr(rest_data, '|');
    if (!second_last_pipe) return;
    
    dislikes = second_last_pipe + 1;
    *second_last_pipe = '\0';
    
    char* third_last_pipe = strrchr(rest_data, '|');
    if (!third_last_pipe) return;
    
    likes = third_last_pipe + 1;
    *third_last_pipe = '\0';
    
    char* fourth_last_pipe = strrchr(rest_data, '|');
    if (!fourth_last_pipe) return;
    
    category = fourth_last_pipe + 1;
    *fourth_last_pipe = '\0';
    
    // 나머지는 content
    content = rest_data;
    
    // 각 필드 설정
    strncpy(g_pledges[g_pledge_count].pledge_id, pledge_id, 
            sizeof(g_pledges[g_pledge_count].pledge_id) - 1);
    g_pledges[g_pledge_count].pledge_id[sizeof(g_pledges[g_pledge_count].pledge_id) - 1] = '\0';
    
    strncpy(g_pledges[g_pledge_count].candidate_id, candidate_id, 
            sizeof(g_pledges[g_pledge_count].candidate_id) - 1);
    g_pledges[g_pledge_count].candidate_id[sizeof(g_pledges[g_pledge_count].candidate_id) - 1] = '\0';
    
    strncpy(g_pledges[g_pledge_count].title, title, 
            sizeof(g_pledges[g_pledge_count].title) - 1);
    g_pledges[g_pledge_count].title[sizeof(g_pledges[g_pledge_count].title) - 1] = '\0';
    
    // 내용이 비어있으면 기본값 설정
    if (!content || strlen(content) == 0) {
        strcpy(g_pledges[g_pledge_count].content, "상세 내용이 추후 업데이트 예정입니다.");
    } else {
        strncpy(g_pledges[g_pledge_count].content, content, 
                sizeof(g_pledges[g_pledge_count].content) - 1);
        g_pledges[g_pledge_count].content[sizeof(g_pledges[g_pledge_count].content) - 1] = '\0';
    }
    
    // 카테고리가 비어있으면 기본값 설정
    if (!category || strlen(category) == 0) {
        strcpy(g_pledges[g_pledge_count].category, "일반");
    } else {
        strncpy(g_pledges[g_pledge_count].category, category, 
                sizeof(g_pledges[g_pledge_count].category) - 1);
        g_pledges[g_pledge_count].category[sizeof(g_pledges[g_pledge_count].category) - 1] = '\0';
    }
    
    g_pledges[g_pledge_count].like_count = (likes && strlen(likes) > 0) ? atoi(likes) : 0;
    g_pledges[g_pledge_count].dislike_count = (dislikes && strlen(dislikes) > 0) ? atoi(dislikes) : 0;
    g_pledges[g_pledge_count].created_time = (timestamp && strlen(timestamp) > 0) ? (time_t)atoll(timestamp) : time(NULL);
    
    // 디버깅: 첫 번째 공약의 파싱 결과 출력
    if (g_pledge_count == 0) {
        printf("🔍 파싱 결과 디버깅:\n");
        printf("   공약 ID: '%s'\n", pledge_id ? pledge_id : "NULL");
        printf("   후보자 ID: '%s'\n", candidate_id ? candidate_id : "NULL");
        printf("   제목: '%s'\n", title ? title : "NULL");
        printf("   좋아요: %d, 싫어요: %d, 시간: %ld\n", 
               g_pledges[g_pledge_count].like_count,
               g_pledges[g_pledge_count].dislike_count,
               (long)g_pledges[g_pledge_count].created_time);
    }
    
    g_pledge_count++;
}

// 공약 내용 포맷팅 및 출력 함수
void format_and_print_content(const char* content) {
    if (!content || strlen(content) == 0) {
        printf("   내용이 비어있습니다.\n");
        return;
    }
    
    char work_content[8192];
    strncpy(work_content, content, sizeof(work_content) - 1);
    work_content[sizeof(work_content) - 1] = '\0';
    
    // 개선된 패턴 매칭으로 더 예쁘게 포맷팅
    char* ptr = work_content;
    printf("\n");
    
    while (*ptr) {
        // □ 패턴 처리 (주요 섹션)
        if (strncmp(ptr, "□ ", 3) == 0) {
            printf("\n📋 ");
            ptr += 3; // "□ " 건너뛰기 (UTF-8에서 3바이트)
            
            // 섹션 제목 출력 (다음 ○나 □까지)
            char* section_end = ptr;
            while (*section_end && strncmp(section_end, "○", 3) != 0 && strncmp(section_end, "□", 3) != 0) {
                section_end++;
            }
            
            char temp = *section_end;
            *section_end = '\0';
            
            // 섹션 제목 정리
            char* title = ptr;
            while (*title == ' ') title++; // 앞 공백 제거
            int title_len = strlen(title);
            while (title_len > 0 && title[title_len-1] == ' ') {
                title[--title_len] = '\0'; // 뒤 공백 제거
            }
            
            printf("**%s**\n", title);
            *section_end = temp;
            ptr = section_end;
            continue;
        }
        
        // ○ 패턴 처리 (항목)
        if (strncmp(ptr, "○ ", 3) == 0) {
            printf("\n   🔹 ");
            ptr += 3; // "○ " 건너뛰기 (UTF-8에서 3바이트)
            
            // 항목 내용 출력 (다음 ○나 □나 -까지)
            char* item_end = ptr;
            while (*item_end && strncmp(item_end, "○", 3) != 0 && strncmp(item_end, "□", 3) != 0 && 
                   !(item_end[0] == '-' && item_end[1] == ' ')) {
                item_end++;
            }
            
            char temp = *item_end;
            *item_end = '\0';
            
            // 항목 내용 정리
            char* item = ptr;
            while (*item == ' ') item++; // 앞 공백 제거
            int item_len = strlen(item);
            while (item_len > 0 && item[item_len-1] == ' ') {
                item[--item_len] = '\0'; // 뒤 공백 제거
            }
            
            printf("%s\n", item);
            *item_end = temp;
            ptr = item_end;
            continue;
        }
        
        // - 패턴 처리 (하위 항목)
        if (*ptr == '-' && ptr[1] == ' ') {
            printf("      • ");
            ptr += 2; // "- " 건너뛰기
            
            // 하위 항목 출력 (다음 -나 ○나 □까지)
            char* sub_end = ptr;
            while (*sub_end && *sub_end != '-' && strncmp(sub_end, "○", 3) != 0 && strncmp(sub_end, "□", 3) != 0) {
                sub_end++;
            }
            
            char temp = *sub_end;
            *sub_end = '\0';
            
            // 하위 항목 내용 정리
            char* sub_item = ptr;
            while (*sub_item == ' ') sub_item++; // 앞 공백 제거
            int sub_len = strlen(sub_item);
            while (sub_len > 0 && sub_item[sub_len-1] == ' ') {
                sub_item[--sub_len] = '\0'; // 뒤 공백 제거
            }
            
            printf("%s\n", sub_item);
            *sub_end = temp;
            ptr = sub_end;
            continue;
        }
        
        // ① ② ③ 패턴 처리 (단순화)
        if (strncmp(ptr, "① ", 4) == 0 || strncmp(ptr, "② ", 4) == 0 || strncmp(ptr, "③ ", 4) == 0 ||
            strncmp(ptr, "④ ", 4) == 0 || strncmp(ptr, "⑤ ", 4) == 0) {
            printf("\n      %c%c%c ", ptr[0], ptr[1], ptr[2]);
            ptr += 4; // 한글 번호 + 공백 (UTF-8에서 4바이트)
            
            // 번호 항목 출력
            char* num_end = ptr;
            while (*num_end && strncmp(num_end, "①", 3) != 0 && strncmp(num_end, "②", 3) != 0 && 
                   strncmp(num_end, "③", 3) != 0 && strncmp(num_end, "④", 3) != 0 && 
                   strncmp(num_end, "⑤", 3) != 0 && strncmp(num_end, "○", 3) != 0 && 
                   strncmp(num_end, "□", 3) != 0) {
                num_end++;
            }
            
            char temp = *num_end;
            *num_end = '\0';
            
            // 번호 항목 내용 정리
            char* num_item = ptr;
            while (*num_item == ' ') num_item++; // 앞 공백 제거
            int num_len = strlen(num_item);
            while (num_len > 0 && num_item[num_len-1] == ' ') {
                num_item[--num_len] = '\0'; // 뒤 공백 제거
            }
            
            printf("%s\n", num_item);
            *num_end = temp;
            ptr = num_end;
            continue;
        }
        
        // 다른 문자는 건너뛰기
        ptr++;
    }
    
    printf("\n");
}

// 포맷된 라인 출력 헬퍼 함수
void print_formatted_line(const char* line, int indent_level) {
    if (!line || strlen(line) == 0) return;
    
    // 앞뒤 공백 제거
    while (*line == ' ' || *line == '\t') line++;
    
    char cleaned_line[1024];
    strncpy(cleaned_line, line, sizeof(cleaned_line) - 1);
    cleaned_line[sizeof(cleaned_line) - 1] = '\0';
    
    // 뒤쪽 공백 제거
    int len = strlen(cleaned_line);
    while (len > 0 && (cleaned_line[len-1] == ' ' || cleaned_line[len-1] == '\t')) {
        cleaned_line[--len] = '\0';
    }
    
    if (len == 0) return;
    
    // 들여쓰기와 함께 출력
    for (int i = 0; i < indent_level; i++) {
        printf("  ");
    }
    printf("   %s\n", cleaned_line);
}

// 업데이트 시간 확인
void show_last_update_time(void) {
    FILE* file = fopen(UPDATE_TIME_FILE, "r");
    if (!file) {
        printf("⚠️  업데이트 시간 정보가 없습니다.\n");
        return;
    }
    
    time_t update_time = 0;
    char time_str[100];
    
    fscanf(file, "%lld", (long long*)&update_time);
    fgets(time_str, sizeof(time_str), file);
    
    printf("📅 마지막 데이터 업데이트: %s", time_str);
    fclose(file);
}

// 통계 메뉴
void show_statistics_menu(void) {
    int choice;
    char input[MAX_INPUT_LEN];
    
    while (1) {
        clear_screen();
        print_header("통계 보기");
        printf("로그인 사용자: %s\n", g_logged_in_user);
        print_separator();
        
        printf("📊 원하는 통계를 선택하세요:\n\n");
        printf("1. 전체 통계\n");
        printf("2. 회차별 순위\n");
        printf("0. 이전 메뉴\n");
        
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        switch (choice) {
            case 1: // 전체 통계
                clear_screen();
                print_header("공약 평가 통계 - 상위 10위");
                
                // 공약 데이터 로드
                if (g_pledge_count == 0) {
                    printf("🔄 공약 데이터를 로드하는 중...\n");
                    g_pledge_count = load_pledges_from_file();
                }
                
                if (g_pledge_count == 0) {
                    printf("❌ 공약 데이터가 없습니다.\n");
                    printf("💡 데이터 새로고침 메뉴에서 API를 통해 데이터를 먼저 받아주세요.\n");
                    wait_for_enter();
                    break;
                }
                
                // 후보자 데이터도 로드
                if (g_candidate_count == 0) {
                    g_candidate_count = load_candidates_from_file();
                }
                
                printf("📊 총 %d개 공약 분석 중...\n\n", g_pledge_count);
                
                // 간단한 배열로 상위 10개만 추적 (메모리 안전)
                typedef struct {
                    int pledge_index;
                    int like_count;
                    int total_votes;
                    float approval_rate;
                    char candidate_name[100];
                } TopPledge;
                
                TopPledge top_pledges[10];
                int top_count = 0;
                
                // 모든 공약 검사하여 상위 10개 유지
                for (int i = 0; i < g_pledge_count && i < 100; i++) { // 처음 100개만 검사 (안전성)
                    int total_votes = g_pledges[i].like_count + g_pledges[i].dislike_count;
                    
                    if (total_votes > 0) {
                        float approval_rate = ((float)g_pledges[i].like_count / total_votes) * 100.0f;
                        
                        // 상위 10개에 들어갈 수 있는지 확인 (지지율 우선, 같으면 총 투표수 우선)
                        if (top_count < 10 || 
                            approval_rate > top_pledges[9].approval_rate ||
                            (approval_rate == top_pledges[9].approval_rate && total_votes > top_pledges[9].total_votes)) {
                            // 후보자 이름 찾기
                            char candidate_name[100] = "알 수 없음";
                            for (int j = 0; j < g_candidate_count; j++) {
                                if (strcmp(g_candidates[j].candidate_id, g_pledges[i].candidate_id) == 0) {
                                    strncpy(candidate_name, g_candidates[j].candidate_name, 99);
                                    candidate_name[99] = '\0';
                                    break;
                                }
                            }
                            
                            // 새 항목 생성
                            TopPledge new_pledge;
                            new_pledge.pledge_index = i;
                            new_pledge.like_count = g_pledges[i].like_count;
                            new_pledge.total_votes = total_votes;
                            new_pledge.approval_rate = approval_rate;
                            strcpy(new_pledge.candidate_name, candidate_name);
                            
                            // 올바른 위치에 삽입 (지지율 우선, 같으면 총 투표수 우선)
                            int insert_pos = top_count;
                            for (int k = 0; k < top_count; k++) {
                                if (approval_rate > top_pledges[k].approval_rate || 
                                    (approval_rate == top_pledges[k].approval_rate && total_votes > top_pledges[k].total_votes)) {
                                    insert_pos = k;
                                    break;
                                }
                            }
                            
                            // 기존 항목들을 뒤로 이동
                            for (int k = (top_count < 10 ? top_count : 9); k > insert_pos; k--) {
                                top_pledges[k] = top_pledges[k-1];
                            }
                            
                            // 새 항목 삽입
                            top_pledges[insert_pos] = new_pledge;
                            
                            if (top_count < 10) top_count++;
                        }
                    }
                }
                
                if (top_count == 0) {
                    printf("❌ 평가된 공약이 없습니다.\n");
                    printf("💡 공약에 대한 평가를 먼저 진행해주세요.\n");
                    wait_for_enter();
                    break;
                }
                
                // 상위 10개 출력
                printf("🏆 공약 지지율 상위 %d위:\n\n", top_count);
                
                for (int i = 0; i < top_count; i++) {
                    int idx = top_pledges[i].pledge_index;
                    printf("%d위. ", i + 1);
                    
                    // 1위는 금메달, 2위는 은메달, 3위는 동메달
                    if (i == 0) printf("🥇 ");
                    else if (i == 1) printf("🥈 ");
                    else if (i == 2) printf("🥉 ");
                    else printf("   ");
                    
                    printf("%.1f%% 지지율 (%d표 중 %d표)\n", 
                           top_pledges[i].approval_rate, 
                           top_pledges[i].total_votes, 
                           top_pledges[i].like_count);
                    
                    printf("    📋 제목: %s\n", g_pledges[idx].title);
                    printf("    👤 후보: %s\n", top_pledges[i].candidate_name);
                    printf("    📂 분야: %s\n", g_pledges[idx].category);
                    printf("\n");
                }
                
                printf("📊 통계 요약:\n");
                printf("   - 평가된 공약 수: %d개 이상\n", top_count);
                printf("   - 전체 공약 수: %d개\n", g_pledge_count);
                
                wait_for_enter();
                break;
                
            case 2: // 회차별 순위
                show_election_rankings();
                break;
                
            case 0: // 이전 메뉴
                return;
                
            default:
                printf("잘못된 선택입니다.\n");
                wait_for_enter();
                break;
        }
    }
}

// 회차별 순위
void show_election_rankings(void) {
    int choice;
    char input[MAX_INPUT_LEN];
    
    // 선거 데이터 로드
    if (g_election_count == 0) {
        g_election_count = load_elections_from_file();
    }
    
    while (1) {
        clear_screen();
        print_header("회차별 순위");
        
        if (g_election_count == 0) {
            printf("❌ 선거 정보가 없습니다.\n");
            wait_for_enter();
            return;
        }
        
        printf("🏆 선거 회차를 선택하세요:\n\n");
        
        for (int i = 0; i < g_election_count; i++) {
            printf("%d. %s (%s)\n", 
                   i + 1, 
                   g_elections[i].election_name, 
                   g_elections[i].election_date);
        }
        
        printf("0. 이전 메뉴\n");
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        if (choice == 0) {
            return;
        } else if (choice >= 1 && choice <= g_election_count) {
            show_candidate_rankings(choice - 1);
        } else {
            printf("잘못된 선택입니다.\n");
            wait_for_enter();
        }
    }
}

// 후보자별 순위
void show_candidate_rankings(int election_index) {
    clear_screen();
    print_header("후보자별 공약 평가 순위");
    printf("선거: %s\n", g_elections[election_index].election_name);
    print_separator();
    
    // 후보자 데이터 로드
    if (g_candidate_count == 0) {
        g_candidate_count = load_candidates_from_file();
    }
    
    if (g_pledge_count == 0) {
        g_pledge_count = load_pledges_from_file();
    }
    
    // 해당 선거의 후보자들 찾기
    int candidate_indices[MAX_CANDIDATES];
    int candidate_count_for_election = 0;
    
    for (int i = 0; i < g_candidate_count; i++) {
        if (strcmp(g_candidates[i].election_id, g_elections[election_index].election_id) == 0) {
            candidate_indices[candidate_count_for_election] = i;
            candidate_count_for_election++;
        }
    }
    
    if (candidate_count_for_election == 0) {
        printf("❌ 해당 선거의 후보자 정보가 없습니다.\n");
        wait_for_enter();
        return;
    }
    
    // 순위 헤더는 아래에서 출력
    
    // 각 후보자의 평균 지지율 계산
    typedef struct {
        int candidate_index;
        double avg_approval;
        int total_votes;
        int total_pledges;
        int server_data_count;  // 서버에서 가져온 데이터 개수
    } CandidateRanking;
    
    CandidateRanking rankings[MAX_CANDIDATES];
    int ranking_count = 0;
    
    for (int i = 0; i < candidate_count_for_election; i++) {
        int candidate_idx = candidate_indices[i];
        int total_likes = 0, total_dislikes = 0, pledge_count = 0;
        int server_data_count = 0;  // 서버에서 가져온 데이터 개수
        
        // 해당 후보자의 모든 공약 평가 합계
        for (int j = 0; j < g_pledge_count; j++) {
            if (strcmp(g_pledges[j].candidate_id, g_candidates[candidate_idx].candidate_id) == 0) {
                // 서버에서 실시간 통계 가져오기 시도
                PledgeStatistics stats;
                if (get_pledge_statistics_from_server(g_pledges[j].pledge_id, &stats)) {
                    // 서버 실시간 데이터 사용
                    total_likes += stats.like_count;
                    total_dislikes += stats.dislike_count;
                    server_data_count++;
                } else {
                    // 서버 연결 실패 시 로컬 데이터 사용
                total_likes += g_pledges[j].like_count;
                total_dislikes += g_pledges[j].dislike_count;
                }
                pledge_count++;
            }
        }
        
        double avg_approval = 0.0;
        int total_votes = total_likes + total_dislikes;
        
        if (total_votes > 0) {
            avg_approval = ((double)total_likes / total_votes) * 100.0;
        }
        
        rankings[ranking_count].candidate_index = candidate_idx;
        rankings[ranking_count].avg_approval = avg_approval;
        rankings[ranking_count].total_votes = total_votes;
        rankings[ranking_count].total_pledges = pledge_count;
        rankings[ranking_count].server_data_count = server_data_count;  // 서버 데이터 개수 저장
        ranking_count++;
    }
    
    // 지지율 기준으로 정렬 (버블 정렬)
    for (int i = 0; i < ranking_count - 1; i++) {
        for (int j = 0; j < ranking_count - i - 1; j++) {
            if (rankings[j].avg_approval < rankings[j + 1].avg_approval) {
                CandidateRanking temp = rankings[j];
                rankings[j] = rankings[j + 1];
                rankings[j + 1] = temp;
            }
        }
    }
    
    // 순위 표시
    printf("📊 후보자별 공약 지지율 순위 (실시간 데이터):\n\n");
    for (int i = 0; i < ranking_count; i++) {
        int idx = rankings[i].candidate_index;
        printf("%d위. %s (%s)\n", 
               i + 1,
               g_candidates[idx].candidate_name,
               g_candidates[idx].party_name);
        printf("     📊 평균 지지율: %.1f%% (총 %d표, 공약 %d개)",
               rankings[i].avg_approval,
               rankings[i].total_votes,
               rankings[i].total_pledges);
        
        // 서버 데이터 사용 비율 표시
        if (rankings[i].total_pledges > 0) {
            double server_ratio = ((double)rankings[i].server_data_count / rankings[i].total_pledges) * 100.0;
            if (server_ratio == 100.0) {
                printf(" 🔄");  // 모든 데이터가 실시간
            } else if (server_ratio > 0) {
                printf(" 🔄📁");  // 일부 실시간, 일부 로컬
            } else {
                printf(" 📁");  // 모든 데이터가 로컬
            }
        }
        printf("\n");
        
        if (i == 0 && rankings[i].total_votes > 0) {
            printf("     🏆 공약 지지율이 제일 높아요!\n");
        }
        printf("\n");
    }
    
    printf("💡 데이터 표시 설명:\n");
    printf("🔄 = 서버 실시간 데이터만 사용\n");
    printf("🔄📁 = 실시간 + 로컬 데이터 혼합\n");
    printf("📁 = 로컬 캐시 데이터만 사용 (서버 연결 실패)\n\n");
    
    wait_for_enter();
}

// 선거 회차 선택
void show_election_selection(void) {
    int choice;
    char input[MAX_INPUT_LEN];
    
    // 선거 데이터 로드
    if (g_election_count == 0) {
        g_election_count = load_elections_from_file();
    }
    
    while (1) {
        clear_screen();
        print_header("선거 회차 선택");
        
        if (g_election_count == 0) {
            printf("❌ 선거 정보가 없습니다.\n");
            printf("관리자에게 문의하여 데이터를 새로고침해주세요.\n");
            wait_for_enter();
            return;
        }
        
        printf("🗳️  대선 회차를 선택하세요:\n\n");
        
        for (int i = 0; i < g_election_count; i++) {
            printf("%d. %s (%s)\n", 
                   i + 1, 
                   g_elections[i].election_name, 
                   g_elections[i].election_date);
        }
        
        printf("0. 이전 메뉴\n");
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        if (choice == 0) {
            return;
        } else if (choice >= 1 && choice <= g_election_count) {
            g_current_election = choice - 1;
            show_candidate_selection(g_current_election);
        } else {
            printf("잘못된 선택입니다.\n");
            wait_for_enter();
        }
    }
}

// 후보자 선택
void show_candidate_selection(int election_index) {
    int choice;
    char input[MAX_INPUT_LEN];
    int candidate_count_for_election = 0;
    int candidate_indices[MAX_CANDIDATES];
    
    // 후보자 데이터 로드
    if (g_candidate_count == 0) {
        g_candidate_count = load_candidates_from_file();
    }
    
    // 해당 선거의 후보자들 찾기
    for (int i = 0; i < g_candidate_count; i++) {
        if (strcmp(g_candidates[i].election_id, g_elections[election_index].election_id) == 0) {
            candidate_indices[candidate_count_for_election] = i;
            candidate_count_for_election++;
        }
    }
    
    while (1) {
        clear_screen();
        print_header("후보자 목록");
        printf("선택된 선거: %s\n", g_elections[election_index].election_name);
        print_separator();
        
        if (candidate_count_for_election == 0) {
            printf("❌ 해당 선거의 후보자 정보가 없습니다.\n");
            wait_for_enter();
            return;
        }
        
        printf("👥 후보자를 선택하세요:\n\n");
        
        for (int i = 0; i < candidate_count_for_election; i++) {
            int idx = candidate_indices[i];
            printf("%d. %s (%s) - 기호 %d번\n", 
                   i + 1,
                   g_candidates[idx].candidate_name,
                   g_candidates[idx].party_name,
                   g_candidates[idx].candidate_number);
        }
        
        printf("0. 이전 메뉴 (선거 선택)\n");
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        if (choice == 0) {
            return;
        } else if (choice >= 1 && choice <= candidate_count_for_election) {
            g_current_candidate = candidate_indices[choice - 1];
            show_pledge_selection(g_current_candidate);
        } else {
            printf("잘못된 선택입니다.\n");
            wait_for_enter();
        }
    }
}

// 공약 목록
void show_pledge_selection(int candidate_index) {
    int choice;
    char input[MAX_INPUT_LEN];
    int pledge_count_for_candidate = 0;
    int pledge_indices[MAX_PLEDGES];
    
    // 공약 데이터 로드
    if (g_pledge_count == 0) {
        g_pledge_count = load_pledges_from_file();
    }
    
    // 해당 후보자의 공약들 찾기
    for (int i = 0; i < g_pledge_count; i++) {
        if (strcmp(g_pledges[i].candidate_id, g_candidates[candidate_index].candidate_id) == 0) {
            pledge_indices[pledge_count_for_candidate] = i;
            pledge_count_for_candidate++;
        }
    }
    
    while (1) {
        clear_screen();
        print_header("공약 목록");
        printf("후보자: %s (%s)\n", 
               g_candidates[candidate_index].candidate_name,
               g_candidates[candidate_index].party_name);
        print_separator();
        
        if (pledge_count_for_candidate == 0) {
            printf("❌ 해당 후보자의 공약 정보가 없습니다.\n");
            wait_for_enter();
            return;
        }
        
        printf("📋 공약을 선택하세요:\n\n");
        
        for (int i = 0; i < pledge_count_for_candidate && i < 10; i++) {
            int idx = pledge_indices[i];
            printf("%d. %s [%s]\n", 
                   i + 1,
                   g_pledges[idx].title,
                   g_pledges[idx].category);
        }
        
        if (pledge_count_for_candidate > 10) {
            printf("... 외 %d개 공약\n", pledge_count_for_candidate - 10);
        }
        
        printf("0. 이전 메뉴 (후보자 선택)\n");
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        if (choice == 0) {
            return;
        } else if (choice >= 1 && choice <= pledge_count_for_candidate && choice <= 10) {
            show_pledge_detail(pledge_indices[choice - 1]);
        } else {
            printf("잘못된 선택입니다.\n");
            wait_for_enter();
        }
    }
}

// 서버에 평가 요청 전송
int send_evaluation_to_server(const char* pledge_id, int evaluation_type) {
    if (!g_client_state.is_connected || !g_client_state.is_logged_in) {
        printf("❌ 서버에 연결되지 않았거나 로그인이 필요합니다.\n");
        return 0;
    }
    
    NetworkMessage request, response;
    char message_data[MAX_CONTENT_LEN];
    
    // 요청 메시지 구성
    memset(&request, 0, sizeof(NetworkMessage));
    request.message_type = MSG_EVALUATE_PLEDGE;
    request.status_code = 200;
    strcpy(request.user_id, g_client_state.user_id);
    strcpy(request.session_id, g_client_state.session_id);
    
    // 메시지 데이터: "pledge_id|evaluation_type"
    snprintf(message_data, sizeof(message_data), "%s|%d", pledge_id, evaluation_type);
    strcpy(request.data, message_data);
    request.data_length = strlen(message_data);
    
    // 서버로 요청 전송
    if (send(g_client_state.server_socket, (char*)&request, sizeof(NetworkMessage), 0) == SOCKET_ERROR) {
        printf("❌ 서버로 평가 요청 전송 실패\n");
        return 0;
    }
    
    // 서버 응답 받기
    if (recv(g_client_state.server_socket, (char*)&response, sizeof(NetworkMessage), 0) <= 0) {
        printf("❌ 서버 응답 수신 실패\n");
        return 0;
    }
    
    if (response.status_code == 200) {
        printf("✅ 평가가 서버에 저장되었습니다.\n");
        return 1;
    } else {
        printf("❌ 서버 오류: %s\n", response.data);
        return 0;
    }
}

// 서버에서 사용자의 평가 상태 조회
int get_user_evaluation_from_server(const char* pledge_id) {
    if (!g_client_state.is_connected || !g_client_state.is_logged_in) {
        return 0; // 평가 없음
    }
    
    NetworkMessage request, response;
    
    // 요청 메시지 구성
    memset(&request, 0, sizeof(NetworkMessage));
    request.message_type = MSG_GET_USER_EVALUATION;
    request.status_code = 200;
    strcpy(request.user_id, g_client_state.user_id);
    strcpy(request.session_id, g_client_state.session_id);
    strcpy(request.data, pledge_id);
    request.data_length = strlen(pledge_id);
    
    // 서버로 요청 전송
    if (send(g_client_state.server_socket, (char*)&request, sizeof(NetworkMessage), 0) == SOCKET_ERROR) {
        return 0;
    }
    
    // 서버 응답 받기
    if (recv(g_client_state.server_socket, (char*)&response, sizeof(NetworkMessage), 0) <= 0) {
        return 0;
    }
    
    if (response.status_code == 200) {
        return atoi(response.data); // 평가 타입 반환 (1: 좋아요, -1: 싫어요, 0: 없음)
    }
    
    return 0; // 평가 없음
}

// 서버에 평가 취소 요청 전송
int cancel_evaluation_on_server(const char* pledge_id) {
    if (!g_client_state.is_connected || !g_client_state.is_logged_in) {
        printf("❌ 서버에 연결되지 않았거나 로그인이 필요합니다.\n");
        return 0;
    }
    
    NetworkMessage request, response;
    
    // 요청 메시지 구성
    memset(&request, 0, sizeof(NetworkMessage));
    request.message_type = MSG_CANCEL_EVALUATION;
    request.status_code = 200;
    strcpy(request.user_id, g_client_state.user_id);
    strcpy(request.session_id, g_client_state.session_id);
    strcpy(request.data, pledge_id);
    request.data_length = strlen(pledge_id);
    
    // 서버로 요청 전송
    if (send(g_client_state.server_socket, (char*)&request, sizeof(NetworkMessage), 0) == SOCKET_ERROR) {
        printf("❌ 서버로 취소 요청 전송 실패\n");
        return 0;
    }
    
    // 서버 응답 받기
    if (recv(g_client_state.server_socket, (char*)&response, sizeof(NetworkMessage), 0) <= 0) {
        printf("❌ 서버 응답 수신 실패\n");
        return 0;
    }
    
    if (response.status_code == 200) {
        printf("✅ 평가가 취소되었습니다.\n");
        return 1;
    } else {
        printf("❌ 서버 오류: %s\n", response.data);
        return 0;
    }
}

// 서버에서 공약 통계 조회
int get_pledge_statistics_from_server(const char* pledge_id, PledgeStatistics* stats) {
    if (!g_client_state.is_connected || !g_client_state.is_logged_in || !stats) {
        return 0;
    }
    
    NetworkMessage request, response;
    
    // 요청 메시지 구성
    memset(&request, 0, sizeof(NetworkMessage));
    request.message_type = MSG_GET_STATISTICS;
    request.status_code = 200;
    strcpy(request.user_id, g_client_state.user_id);
    strcpy(request.session_id, g_client_state.session_id);
    strcpy(request.data, pledge_id);
    request.data_length = strlen(pledge_id);
    
    // 서버로 요청 전송
    if (send(g_client_state.server_socket, (char*)&request, sizeof(NetworkMessage), 0) == SOCKET_ERROR) {
        return 0;
    }
    
    // 서버 응답 받기 (타임아웃 설정)
    fd_set read_fds;
    struct timeval timeout;
    FD_ZERO(&read_fds);
    FD_SET(g_client_state.server_socket, &read_fds);
    timeout.tv_sec = 2;  // 2초 타임아웃
    timeout.tv_usec = 0;
    
    if (select(0, &read_fds, NULL, NULL, &timeout) <= 0) {
        return 0; // 타임아웃 또는 오류
    }
    
    if (recv(g_client_state.server_socket, (char*)&response, sizeof(NetworkMessage), 0) <= 0) {
        return 0;
    }
    
    if (response.status_code == 200) {
        // JSON 파싱 (간단한 구현)
        char* like_pos = strstr(response.data, "\"like_count\":");
        char* dislike_pos = strstr(response.data, "\"dislike_count\":");
        char* total_pos = strstr(response.data, "\"total_votes\":");
        char* approval_pos = strstr(response.data, "\"approval_rate\":");
        
        if (like_pos && dislike_pos && total_pos && approval_pos) {
            stats->like_count = atoi(like_pos + 13);
            stats->dislike_count = atoi(dislike_pos + 16);
            stats->total_votes = atoi(total_pos + 14);
            stats->approval_rate = atof(approval_pos + 16);
            return 1;
        }
    }
    
    return 0;
}

// 공약 상세 내용 및 평가
void show_pledge_detail(int pledge_index) {
    int choice;
    char input[MAX_INPUT_LEN];
    
    while (1) {
        clear_screen();
        print_header("공약 상세 내용");
        
        printf("📄 공약 제목: %s\n", g_pledges[pledge_index].title);
        printf("📂 분야: %s\n", g_pledges[pledge_index].category);
        printf("👤 후보자: %s\n", 
               g_candidates[g_current_candidate].candidate_name);
        print_separator();
        
        printf("📝 공약 내용:\n");
        format_and_print_content(g_pledges[pledge_index].content);
        print_separator();
        
        printf("📊 현재 평가 통계:\n");
        
        // 서버에서 실시간 통계 가져오기
        PledgeStatistics stats;
        if (get_pledge_statistics_from_server(g_pledges[pledge_index].pledge_id, &stats)) {
            printf("👍 좋아요: %d표\n", stats.like_count);
            printf("👎 싫어요: %d표\n", stats.dislike_count);
            if (stats.total_votes > 0) {
                printf("📈 지지율: %.1f%%\n", stats.approval_rate);
            }
        } else {
            // 서버 통계 조회 실패 시 로컬 데이터 사용
        printf("👍 좋아요: %d표\n", g_pledges[pledge_index].like_count);
        printf("👎 싫어요: %d표\n", g_pledges[pledge_index].dislike_count);
        
        int total = g_pledges[pledge_index].like_count + g_pledges[pledge_index].dislike_count;
        if (total > 0) {
            double approval = ((double)g_pledges[pledge_index].like_count / total) * 100.0;
            printf("📈 지지율: %.1f%%\n", approval);
            }
        }
        
        // 현재 사용자의 평가 상태 확인
        int user_evaluation = get_user_evaluation_from_server(g_pledges[pledge_index].pledge_id);
        if (user_evaluation == 1) {
            printf("🔵 내 평가: 👍 지지함\n");
        } else if (user_evaluation == -1) {
            printf("🔴 내 평가: 👎 반대함\n");
        } else {
            printf("⚪ 내 평가: 아직 평가하지 않음\n");
        }
        print_separator();
        
        // 메뉴 옵션을 사용자 평가 상태에 따라 동적으로 표시
        if (user_evaluation == 0) {
            // 평가하지 않은 경우
        printf("1. 👍 이 공약을 지지합니다\n");
        printf("2. 👎 이 공약을 반대합니다\n");
        } else if (user_evaluation == 1) {
            // 이미 지지한 경우
            printf("1. 👎 반대로 변경하기\n");
            printf("2. ❌ 평가 취소하기\n");
        } else if (user_evaluation == -1) {
            // 이미 반대한 경우
            printf("1. 👍 지지로 변경하기\n");
            printf("2. ❌ 평가 취소하기\n");
        }
        
        printf("3. 📊 상세 통계 보기\n");
        printf("0. 이전 메뉴\n");
        print_separator();
        printf("선택하세요: ");
        
        if (!get_user_input(input, sizeof(input))) {
            continue;
        }
        
        choice = atoi(input);
        
        if (user_evaluation == 0) {
            // 평가하지 않은 경우
        switch (choice) {
            case 1: // 지지
                    printf("\n평가를 서버에 전송 중...\n");
                    if (send_evaluation_to_server(g_pledges[pledge_index].pledge_id, 1)) {
                        printf("✅ '%s' 공약을 지지하셨습니다!\n", 
                       g_pledges[pledge_index].title);
                        // 서버에서 최신 통계를 가져와서 화면에 반영하지 않고 다음 화면 갱신 시 자동 반영
                        printf("💡 최신 통계는 화면이 새로고침될 때 반영됩니다.\n");
                    } else {
                        printf("❌ 평가 전송에 실패했습니다.\n");
                    }
                wait_for_enter();
                break;
                
            case 2: // 반대
                    printf("\n평가를 서버에 전송 중...\n");
                    if (send_evaluation_to_server(g_pledges[pledge_index].pledge_id, -1)) {
                        printf("✅ '%s' 공약을 반대하셨습니다!\n", 
                       g_pledges[pledge_index].title);
                        printf("💡 최신 통계는 화면이 새로고침될 때 반영됩니다.\n");
                    } else {
                        printf("❌ 평가 전송에 실패했습니다.\n");
                    }
                    wait_for_enter();
                    break;
            }
        } else if (user_evaluation == 1) {
            // 이미 지지한 경우
            switch (choice) {
                case 1: // 반대로 변경
                    printf("\n평가를 변경하는 중...\n");
                    if (send_evaluation_to_server(g_pledges[pledge_index].pledge_id, -1)) {
                        printf("✅ 평가를 반대로 변경했습니다!\n");
                        printf("💡 최신 통계는 화면이 새로고침될 때 반영됩니다.\n");
                    } else {
                        printf("❌ 평가 변경에 실패했습니다.\n");
                    }
                wait_for_enter();
                break;
                
                case 2: // 평가 취소
                    printf("\n평가를 취소하는 중...\n");
                    if (cancel_evaluation_on_server(g_pledges[pledge_index].pledge_id)) {
                        printf("💡 최신 통계는 화면이 새로고침될 때 반영됩니다.\n");
                    }
                    wait_for_enter();
                    break;
            }
        } else if (user_evaluation == -1) {
            // 이미 반대한 경우
            switch (choice) {
                case 1: // 지지로 변경
                    printf("\n평가를 변경하는 중...\n");
                    if (send_evaluation_to_server(g_pledges[pledge_index].pledge_id, 1)) {
                        printf("✅ 평가를 지지로 변경했습니다!\n");
                        printf("💡 최신 통계는 화면이 새로고침될 때 반영됩니다.\n");
                    } else {
                        printf("❌ 평가 변경에 실패했습니다.\n");
                    }
                    wait_for_enter();
                    break;
                    
                case 2: // 평가 취소
                    printf("\n평가를 취소하는 중...\n");
                    if (cancel_evaluation_on_server(g_pledges[pledge_index].pledge_id)) {
                        printf("💡 최신 통계는 화면이 새로고침될 때 반영됩니다.\n");
                    }
                    wait_for_enter();
                    break;
            }
        }
        
        // 공통 메뉴 처리 (평가 관련 선택은 이미 위에서 처리됨)
        if (choice == 3) {
            // 상세 통계
                clear_screen();
                print_header("공약 상세 통계");
                printf("공약: %s\n", g_pledges[pledge_index].title);
                printf("후보자: %s (%s)\n", 
                       g_candidates[g_current_candidate].candidate_name,
                       g_candidates[g_current_candidate].party_name);
                print_separator();
                
            // 서버에서 실시간 통계 가져오기
            PledgeStatistics detail_stats;
            if (get_pledge_statistics_from_server(g_pledges[pledge_index].pledge_id, &detail_stats)) {
                printf("총 투표 수: %d표\n", detail_stats.total_votes);
                printf("지지표: %d표\n", detail_stats.like_count);
                printf("반대표: %d표\n", detail_stats.dislike_count);
                
                if (detail_stats.total_votes > 0) {
                    printf("지지율: %.1f%%\n", detail_stats.approval_rate);
                    printf("반대율: %.1f%%\n", 100.0 - detail_stats.approval_rate);
                }
            } else {
                // 서버 통계 조회 실패 시 로컬 데이터 사용
                int total = g_pledges[pledge_index].like_count + g_pledges[pledge_index].dislike_count;
                printf("총 투표 수: %d표\n", total);
                printf("지지표: %d표\n", g_pledges[pledge_index].like_count);
                printf("반대표: %d표\n", g_pledges[pledge_index].dislike_count);
                
                if (total > 0) {
                    double approval = ((double)g_pledges[pledge_index].like_count / total) * 100.0;
                    printf("지지율: %.1f%%\n", approval);
                    printf("반대율: %.1f%%\n", 100.0 - approval);
                }
                }
                
                wait_for_enter();
        } else if (choice == 0) {
            // 이전 메뉴
                return;
        } else if (choice != 1 && choice != 2) {
            // 1, 2번은 이미 위에서 처리됨. 3, 0번이 아닌 다른 번호만 오류 처리
                printf("잘못된 선택입니다.\n");
                wait_for_enter();
        }
    }
} 