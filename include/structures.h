#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <time.h>

// 최대 값 정의
#define MAX_STRING_LEN 256
#define MAX_CONTENT_LEN 2048
#define MAX_USERS 100
#define MAX_ELECTIONS 200  // 50 → 200으로 증가 (181개 + 여유분)
#define MAX_CANDIDATES 10000
#define MAX_PLEDGES 100000

// 선거 정보 구조체
typedef struct {
    char election_id[MAX_STRING_LEN];      // 선거 ID
    char election_name[MAX_STRING_LEN];    // 선거명
    char election_date[MAX_STRING_LEN];    // 선거일
    char election_type[MAX_STRING_LEN];    // 선거 종류
    int is_active;                         // 활성 상태
} ElectionInfo;

// 후보자 정보 구조체
typedef struct {
    char candidate_id[MAX_STRING_LEN];     // 후보자 ID
    char candidate_name[MAX_STRING_LEN];   // 후보자명
    char party_name[MAX_STRING_LEN];       // 정당명
    int candidate_number;                  // 기호번호
    char election_id[MAX_STRING_LEN];      // 소속 선거 ID
    int pledge_count;                      // 공약 수
} CandidateInfo;

// 공약 정보 구조체
typedef struct {
    char pledge_id[MAX_STRING_LEN];        // 공약 ID
    char candidate_id[MAX_STRING_LEN];     // 후보자 ID
    char title[MAX_STRING_LEN];            // 공약 제목
    char content[MAX_CONTENT_LEN];         // 공약 내용
    char category[MAX_STRING_LEN];         // 공약 분야
    int like_count;                        // 좋아요 수
    int dislike_count;                     // 싫어요 수
    time_t created_time;                   // 생성 시간
} PledgeInfo;

// 사용자 정보 구조체
typedef struct {
    char user_id[MAX_STRING_LEN];          // 사용자 ID
    char password_hash[MAX_STRING_LEN];    // 비밀번호 해시
    int login_attempts;                    // 로그인 시도 횟수
    int is_locked;                         // 계정 잠금 상태
    time_t last_login;                     // 마지막 로그인 시간
    int is_online;                         // 온라인 상태
    char session_id[MAX_STRING_LEN];       // 세션 ID
} UserInfo;

// 평가 정보 구조체
typedef struct {
    char user_id[MAX_STRING_LEN];          // 사용자 ID
    char pledge_id[MAX_STRING_LEN];        // 공약 ID
    int evaluation_type;                   // 평가 유형 (1: 좋아요, -1: 싫어요, 0: 취소/없음)
    time_t evaluation_time;                // 평가 시간
} EvaluationInfo;

// 서버 클라이언트 통신 메시지 구조체
typedef struct {
    int message_type;                      // 메시지 타입
    char user_id[MAX_STRING_LEN];          // 사용자 ID
    char session_id[MAX_STRING_LEN];       // 세션 ID
    char data[MAX_CONTENT_LEN];            // 데이터
    int data_length;                       // 데이터 길이
    int status_code;                       // 상태 코드
} NetworkMessage;

// 메시지 타입 정의
typedef enum {
    MSG_LOGIN_REQUEST = 1,
    MSG_LOGIN_RESPONSE,
    MSG_LOGOUT_REQUEST,
    MSG_GET_ELECTIONS,
    MSG_GET_CANDIDATES,
    MSG_GET_PLEDGES,
    MSG_EVALUATE_PLEDGE,
    MSG_CANCEL_EVALUATION,      // 평가 취소
    MSG_GET_USER_EVALUATION,    // 사용자의 특정 공약 평가 조회
    MSG_GET_STATISTICS,
    MSG_REFRESH_ELECTIONS,      // 선거 정보 새로고침
    MSG_REFRESH_CANDIDATES,     // 후보자 정보 새로고침  
    MSG_REFRESH_PLEDGES,        // 공약 정보 새로고침
    MSG_REFRESH_ALL,            // 전체 데이터 새로고침
    MSG_ERROR,
    MSG_SUCCESS
} MessageType;

// 응답 상태 코드 정의
typedef enum {
    STATUS_SUCCESS = 200,
    STATUS_BAD_REQUEST = 400,
    STATUS_UNAUTHORIZED = 401,
    STATUS_NOT_FOUND = 404,
    STATUS_INTERNAL_ERROR = 500
} StatusCode;

#endif // STRUCTURES_H 