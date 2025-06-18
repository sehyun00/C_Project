#ifndef API_H
#define API_H

#include "structures.h"

#ifdef _WIN32
    #include <windows.h>
    #include <wininet.h>
#else
    #include <curl/curl.h>
#endif

// API 기본 설정
#define API_BASE_URL "http://apis.data.go.kr"
#define API_KEY_FILE "data/api_key.txt"
#define MAX_URL_LEN 1024
#define MAX_RESPONSE_SIZE 1048576  // 1MB

// API 엔드포인트
#define ELECTION_CODE_API "/CommonCodeService/getCommonSgCodeInfoInqire"
#define CANDIDATE_API "/PofelcddInfoInqireService/getPoelpcddRegistSttusInfoInqire"  
#define PLEDGE_API "/ElecPrmsInfoInqireService/getCnddtElecPrmsInfoInqire"

// HTTP 응답 구조체
typedef struct {
    char* data;
    size_t size;
} APIResponse;

// API 클라이언트 구조체
typedef struct {
#ifdef _WIN32
    HINTERNET hInternet;
    HINTERNET hConnect;
#else
    CURL* curl;
#endif
    char api_key[MAX_STRING_LEN];
    int is_initialized;
} APIClient;

// API 초기화 및 정리
int init_api_client(APIClient* client);
void cleanup_api_client(APIClient* client);
int load_api_key(const char* filename, char* api_key);

// HTTP 요청 함수
size_t write_callback(void* contents, size_t size, size_t nmemb, APIResponse* response);
int make_api_request(APIClient* client, const char* url, APIResponse* response);
int http_request(const char* url, char* response_buffer, size_t buffer_size);

// 새로운 API 함수들
char* url_encode(const char* str);
int api_get_election_info(APIClient* client, char* response_buffer, size_t buffer_size);
int api_get_candidate_info(APIClient* client, const char* election_id, char* response_buffer, size_t buffer_size);
int api_get_pledge_info(APIClient* client, const char* election_id, const char* candidate_id, char* response_buffer, size_t buffer_size);

// URL 생성 함수
void build_election_code_url(const char* api_key, char* url);
void build_candidate_url(const char* api_key, const char* sg_id, const char* sg_typecode, char* url);
void build_pledge_url(const char* api_key, const char* sg_id, const char* sg_typecode, const char* cnddt_id, char* url);

// API 호출 함수
int get_election_codes(APIClient* client, ElectionInfo elections[], int max_elections);
int get_candidates(APIClient* client, const char* sg_id, const char* sg_typecode, 
                   CandidateInfo candidates[], int max_candidates);
int get_pledges(APIClient* client, const char* sg_id, const char* sg_typecode, 
                const char* cnddt_id, PledgeInfo pledges[], int max_pledges);

// JSON 파싱 함수
int parse_election_json(const char* json_data, ElectionInfo elections[], int max_elections);
int parse_candidate_json(const char* json_data, const char* election_id, CandidateInfo candidates[], int max_candidates);
int parse_pledge_json(const char* json_data, PledgeInfo pledges[], int max_pledges);

// 유틸리티 함수
int validate_api_response(const char* json_data);
void print_api_error(const char* function_name, const char* error_message);

#endif // API_H 