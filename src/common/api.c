#include "api.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <wininet.h>
    #pragma comment(lib, "wininet.lib")
#else
    #include <curl/curl.h>
#endif

// API 클라이언트 초기화
int init_api_client(APIClient* client) {
    if (!client) return 0;
    
    write_log("INFO", "API 클라이언트 초기화 중...");
    
#ifdef _WIN32
    // Windows Internet API 초기화
    client->hInternet = InternetOpenA("ElectionAPI/1.0", 
                                     INTERNET_OPEN_TYPE_PRECONFIG, 
                                     NULL, NULL, 0);
    if (!client->hInternet) {
        write_error_log("init_api_client", "WinINet 초기화 실패");
        return 0;
    }
#else
    // curl 전역 초기화
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        write_error_log("init_api_client", "curl 전역 초기화 실패");
        return 0;
    }
    
    // curl 핸들 생성
    client->curl = curl_easy_init();
    if (!client->curl) {
        write_error_log("init_api_client", "curl 핸들 생성 실패");
        curl_global_cleanup();
        return 0;
    }
#endif
    
    // API 키 로드
    if (!load_api_key(API_KEY_FILE, client->api_key)) {
        write_error_log("init_api_client", "API 키 로드 실패");
#ifdef _WIN32
        InternetCloseHandle(client->hInternet);
#else
        curl_easy_cleanup(client->curl);
        curl_global_cleanup();
#endif
        return 0;
    }
    
    client->is_initialized = 1;
    write_log("INFO", "API 클라이언트 초기화 완료");
    return 1;
}

// API 클라이언트 정리
void cleanup_api_client(APIClient* client) {
    if (!client || !client->is_initialized) return;
    
    write_log("INFO", "API 클라이언트 정리 중...");
    
#ifdef _WIN32
    if (client->hConnect) {
        InternetCloseHandle(client->hConnect);
        client->hConnect = NULL;
    }
    if (client->hInternet) {
        InternetCloseHandle(client->hInternet);
        client->hInternet = NULL;
    }
#else
    if (client->curl) {
        curl_easy_cleanup(client->curl);
        client->curl = NULL;
    }
    curl_global_cleanup();
#endif
    
    client->is_initialized = 0;
    write_log("INFO", "API 클라이언트 정리 완료");
}

// API 키 파일 로드
int load_api_key(const char* filename, char* api_key) {
    if (!filename || !api_key) return 0;
    
    // 현재 작업 디렉토리 출력 (디버깅용)
    char current_dir[1024];
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(current_dir), current_dir);
#else
    getcwd(current_dir, sizeof(current_dir));
#endif
    printf("🔍 현재 작업 디렉토리: %s\n", current_dir);
    printf("🔍 API 키 파일 경로: %s\n", filename);
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        write_error_log("load_api_key", "API 키 파일을 열 수 없습니다");
        printf("\n⚠️  API 키 파일이 없습니다!\n");
        printf("📁 %s 파일을 생성하고 공공데이터포털에서 발급받은 API 키를 입력하세요.\n", filename);
        printf("🔗 https://www.data.go.kr 에서 회원가입 후 API 신청\n\n");
        return 0;
    }
    
    if (fgets(api_key, MAX_STRING_LEN, file) == NULL) {
        write_error_log("load_api_key", "API 키 읽기 실패");
        fclose(file);
        return 0;
    }
    
    // 개행 문자 제거
    trim_whitespace(api_key);
    fclose(file);
    
    if (strlen(api_key) == 0) {
        write_error_log("load_api_key", "API 키가 비어있습니다");
        return 0;
    }
    
    write_log("INFO", "API 키 로드 완료");
    return 1;
}

// HTTP 응답 콜백 함수
size_t write_callback(void* contents, size_t size, size_t nmemb, APIResponse* response) {
    size_t total_size = size * nmemb;
    
    if (response->size + total_size >= MAX_RESPONSE_SIZE) {
        write_error_log("write_callback", "응답 크기가 너무 큽니다");
        return 0;
    }
    
    response->data = realloc(response->data, response->size + total_size + 1);
    if (!response->data) {
        write_error_log("write_callback", "메모리 할당 실패");
        return 0;
    }
    
    memcpy(&(response->data[response->size]), contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';
    
    return total_size;
}

// API 요청 실행
int make_api_request(APIClient* client, const char* url, APIResponse* response) {
    if (!client || !client->is_initialized || !url || !response) return 0;
    
    write_log("INFO", "API 요청 시작");
    printf("🌐 API 호출 중: %s\n", url);
    
    // 응답 구조체 초기화
    response->data = malloc(1);
    response->size = 0;
    
#ifdef _WIN32
    // Windows Internet API 사용
    HINTERNET hRequest = InternetOpenUrlA(client->hInternet, url, NULL, 0, 
                                         INTERNET_FLAG_RELOAD, 0);
    if (!hRequest) {
        write_error_log("make_api_request", "URL 열기 실패");
        printf("❌ API 요청 실패: URL 열기 실패\n");
        free(response->data);
        return 0;
    }
    
    // 데이터 읽기
    char buffer[4096];
    DWORD bytesRead;
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response->data = realloc(response->data, response->size + bytesRead + 1);
        if (!response->data) {
            write_error_log("make_api_request", "메모리 할당 실패");
            InternetCloseHandle(hRequest);
            return 0;
        }
        
        memcpy(response->data + response->size, buffer, bytesRead);
        response->size += bytesRead;
    }
    
    response->data[response->size] = '\0';
    InternetCloseHandle(hRequest);
    
#else
    // curl 옵션 설정
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 30L);  // 30초 타임아웃
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    // 요청 실행
    CURLcode res = curl_easy_perform(client->curl);
    
    if (res != CURLE_OK) {
        write_error_log("make_api_request", curl_easy_strerror(res));
        printf("❌ API 요청 실패: %s\n", curl_easy_strerror(res));
        free(response->data);
        return 0;
    }
    
    // HTTP 응답 코드 확인
    long response_code;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    if (response_code != 200) {
        write_error_log("make_api_request", "HTTP 오류");
        printf("❌ HTTP 오류: %ld\n", response_code);
        free(response->data);
        return 0;
    }
#endif
    
    write_log("INFO", "API 요청 완료");
    printf("✅ API 응답 수신 완료 (%zu bytes)\n", response->size);
    return 1;
}

// API 엔드포인트 정의 - 공공데이터포털 문서 기준으로 수정
#define ELECTION_CODE_ENDPOINT "/9760000/CommonCodeService/getCommonSgCodeList"
#define CANDIDATE_INFO_ENDPOINT "/9760000/PofelcddInfoInqireService/getPofelcddRegistSttusInfoInqire"
#define PLEDGE_INFO_ENDPOINT "/9760000/ElecPrmsInfoInqireService/getCnddtElecPrmsInfoInqire"

// URL 인코딩 함수 추가
char* url_encode(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* encoded = malloc(len * 3 + 1); // 최대 3배 크기
    if (!encoded) return NULL;
    
    char* p = encoded;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *p++ = c;
        } else {
            sprintf(p, "%%%02X", c);
            p += 3;
        }
    }
    *p = '\0';
    return encoded;
}

// HTTP 요청 함수 (Windows Internet API 사용) - HTTPS 지원 추가
int http_request(const char* url, char* response_buffer, size_t buffer_size) {
    if (!url || !response_buffer || buffer_size == 0) return -1;
    
#ifdef _WIN32
    printf("🔗 HTTP 요청 시작: %s\n", url);
    
    HINTERNET hInternet = NULL;
    HINTERNET hRequest = NULL;
    int result = -1;
    
    // 응답 버퍼 초기화
    response_buffer[0] = '\0';
    
    // Internet API 초기화
    hInternet = InternetOpenA("ElectionAPI/1.0", 
                             INTERNET_OPEN_TYPE_PRECONFIG, 
                             NULL, NULL, 0);
    if (!hInternet) {
        printf("❌ InternetOpen 실패\n");
        goto cleanup;
    }
    
    // HTTPS 지원을 위한 플래그 추가
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (strncmp(url, "https://", 8) == 0) {
        flags |= INTERNET_FLAG_SECURE;
    }
    
    // URL 요청
    hRequest = InternetOpenUrlA(hInternet, url, NULL, 0, flags, 0);
    if (!hRequest) {
        printf("❌ InternetOpenUrl 실패\n");
        goto cleanup;
    }
    
    // 데이터 읽기
    DWORD bytesRead;
    DWORD totalBytes = 0;
    char buffer[4096];
    
    while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        // 버퍼 크기 검사
        if (totalBytes + bytesRead >= buffer_size - 1) {
            printf("⚠️ 응답 버퍼 크기 초과, 데이터 잘림 (최대: %zu)\n", buffer_size - 1);
            break;
        }
        
        // 안전한 문자열 복사
        buffer[bytesRead] = '\0';
        
        // strcat 대신 더 안전한 방법 사용
        if (totalBytes + bytesRead < buffer_size) {
            memcpy(response_buffer + totalBytes, buffer, bytesRead);
            totalBytes += bytesRead;
            response_buffer[totalBytes] = '\0';
        }
    }
    
    if (totalBytes > 0) {
        printf("✅ HTTP 응답 수신 완료: %lu bytes\n", totalBytes);
        result = 0;
    } else {
        printf("❌ HTTP 응답 데이터 없음\n");
    }
    
cleanup:
    if (hRequest) InternetCloseHandle(hRequest);
    if (hInternet) InternetCloseHandle(hInternet);
    
    return result;
#else
    // Linux용 curl 구현 (필요시)
    return -1;
#endif
}

// 선거 정보 조회 함수 - 성공한 별도 프로그램 방식 적용
int api_get_election_info(APIClient* client, char* response_buffer, size_t buffer_size) {
    if (!client || !response_buffer) {
        write_error_log("api_get_election_info", "잘못된 매개변수");
        return -1;
    }

    // API 키 URL 인코딩
    char* encoded_key = url_encode(client->api_key);
    if (!encoded_key) {
        write_error_log("api_get_election_info", "API 키 인코딩 실패");
        return -1;
    }

    // 페이지별 별도 수집 후 병합 방식 (성공한 방법)
    char page1_buffer[32768];
    char page2_buffer[32768];
    char combined_xml[262144];
    
    int page1_count = 0;
    int page2_count = 0;
    
    // 페이지 1 수집
    printf("📊 페이지 1 수집 중...\n");
    char url1[2048];
    snprintf(url1, sizeof(url1), 
        "http://apis.data.go.kr%s?serviceKey=%s&pageNo=1&numOfRows=100&_type=json",
        ELECTION_CODE_ENDPOINT, encoded_key);

    write_log("INFO", "API 요청 시작");
    printf("🌐 API 호출 중 (페이지 1): %s\n", url1);

    if (http_request(url1, page1_buffer, sizeof(page1_buffer)) == 0) {
        if (!strstr(page1_buffer, "INFO-03") && strstr(page1_buffer, "<items>")) {
            printf("✅ 페이지 1 성공 (%zu bytes)\n", strlen(page1_buffer));
            page1_count = 1;
        } else {
            printf("❌ 페이지 1 실패 또는 데이터 없음\n");
            free(encoded_key);
            return -1;
        }
    } else {
        printf("❌ 페이지 1 API 호출 실패\n");
        free(encoded_key);
        return -1;
    }

    // 페이지 2 수집
    printf("📊 페이지 2 수집 중...\n");
    char url2[2048];
    snprintf(url2, sizeof(url2), 
        "http://apis.data.go.kr%s?serviceKey=%s&pageNo=2&numOfRows=100&_type=json",
        ELECTION_CODE_ENDPOINT, encoded_key);

    printf("🌐 API 호출 중 (페이지 2): %s\n", url2);

    if (http_request(url2, page2_buffer, sizeof(page2_buffer)) == 0) {
        if (strstr(page2_buffer, "INFO-03")) {
            printf("⚠️ 페이지 2: 데이터 없음\n");
        } else if (strstr(page2_buffer, "<items>")) {
            printf("✅ 페이지 2 성공 (%zu bytes)\n", strlen(page2_buffer));
            page2_count = 1;
        } else {
            printf("⚠️ 페이지 2: 응답 형식 확인 필요\n");
        }
    } else {
        printf("❌ 페이지 2 API 호출 실패\n");
    }

    // XML 병합 (성공한 방식)
    printf("🔄 XML 데이터 병합 중...\n");
    
    if (page1_count > 0) {
        // 페이지 1을 기본으로 사용
        strncpy(combined_xml, page1_buffer, sizeof(combined_xml) - 1);
        combined_xml[sizeof(combined_xml) - 1] = '\0';
        
        if (page2_count > 0) {
            // 페이지 2의 <item> 태그들을 추출하여 병합
            char* page2_items_start = strstr(page2_buffer, "<item>");
            char* page2_items_end = strstr(page2_buffer, "</items>");
            
            if (page2_items_start && page2_items_end) {
                // 페이지 1에서 </items> 위치 찾기
                char* main_items_end = strstr(combined_xml, "</items>");
                
                if (main_items_end) {
                    // 병합 실행
                    size_t items_length = page2_items_end - page2_items_start;
                    size_t current_length = strlen(combined_xml);
                    
                    if (current_length + items_length + 100 < sizeof(combined_xml)) {
                        // </items> 뒤로 이동
                        size_t tail_length = strlen(main_items_end);
                        memmove(main_items_end + items_length, main_items_end, tail_length + 1);
                        
                        // 페이지 2 아이템들 삽입
                        memcpy(main_items_end, page2_items_start, items_length);
                        
                        printf("✅ 페이지 2 데이터 병합 성공! (%zu bytes 추가)\n", items_length);
                    } else {
                        printf("❌ 버퍼 크기 부족으로 병합 실패\n");
                    }
                } else {
                    printf("❌ 페이지 1에서 </items> 태그를 찾을 수 없음\n");
                }
            } else {
                printf("❌ 페이지 2에서 <item> 태그를 찾을 수 없음\n");
            }
        }
        
        // 최종 결과 설정
        strncpy(response_buffer, combined_xml, buffer_size - 1);
        response_buffer[buffer_size - 1] = '\0';
    }
    
    free(encoded_key);
    
    if (strlen(response_buffer) > 0) {
        write_log("INFO", "API 요청 완료");
        printf("✅ 전체 API 응답 수신 완료 (%zu bytes)\n", strlen(response_buffer));
        printf("📊 페이지 1 + 페이지 2 병합 완료!\n");
        return 0;
    } else {
        write_error_log("api_get_election_info", "API 요청 실패");
        printf("❌ API 요청 실패\n");
        return -1;
    }
}

// 후보자 정보 조회 함수 수정 - 더미 데이터로 우선 처리
int api_get_candidate_info(APIClient* client, const char* election_id, char* response_buffer, size_t buffer_size) {
    if (!client || !election_id || !response_buffer) {
        write_error_log("api_get_candidate_info", "잘못된 매개변수");
        return -1;
    }

    write_log("INFO", "후보자 정보 API 요청 시작");
    printf("🌐 후보자 정보 수집 중 (선거ID: %s)...\n", election_id);

    // API 키 URL 인코딩
    char* encoded_key = url_encode(client->api_key);
    if (!encoded_key) {
        write_error_log("api_get_candidate_info", "API 키 인코딩 실패");
        return -1;
    }

    // elections.txt에서 해당 선거의 sgTypecode 읽어오기
    int sgTypecode = 1;  // 기본값
    
    // elections.txt에서 해당 선거ID의 sgTypecode 찾기
    FILE* elections_file = fopen("data/elections.txt", "r");
    if (elections_file) {
        char line[1024];
        while (fgets(line, sizeof(line), elections_file)) {
            if (line[0] == '#' || strncmp(line, "COUNT=", 6) == 0) continue;
            
            char file_election_id[32];
            char election_name[256];
            char election_date[32];
            char election_type[32];
            int file_sgTypecode;
            
            if (sscanf(line, "%31[^|]|%255[^|]|%31[^|]|%31[^|]|%d", 
                      file_election_id, election_name, election_date, election_type, &file_sgTypecode) == 5) {
                if (strcmp(file_election_id, election_id) == 0) {
                    // sgTypecode=0 (통합선거)는 건너뛰고 실제 선거 찾기
                    if (file_sgTypecode > 0) {
                        sgTypecode = file_sgTypecode;
                        printf("   📋 elections.txt에서 찾은 sgTypecode: %d (%s)\n", sgTypecode, election_name);
                        break;
                    }
                }
            }
        }
        fclose(elections_file);
    } else {
        printf("   ⚠️  elections.txt 파일을 찾을 수 없어 기본값 사용: sgTypecode=%d\n", sgTypecode);
    }

    printf("   선거종류코드: %d (%s)\n", sgTypecode, sgTypecode == 1 ? "대통령선거" : "국회의원선거");

    // URL 생성 - HTTP 사용 (테스트에서 성공 확인)
    char url[2048];
    snprintf(url, sizeof(url), 
        "http://apis.data.go.kr%s?serviceKey=%s&pageNo=1&numOfRows=100&sgId=%s&sgTypecode=%d",
        CANDIDATE_INFO_ENDPOINT, encoded_key, election_id, sgTypecode);

    int result = http_request(url, response_buffer, buffer_size);
    
    printf("🌐 API 호출 URL: %s\n", url);
    
    if (result == 0) {
        printf("📄 실제 API 응답 (처음 1000자):\n%.1000s\n", response_buffer);
        
        // API 응답 확인
        if (strstr(response_buffer, "SERVICE_KEY_IS_NOT_REGISTERED_ERROR")) {
            printf("❌ 후보자 API 서비스 미등록 오류\n");
            free(encoded_key);
            return -1;
        } else if (strstr(response_buffer, "\"resultCode\":\"00\"") || 
                   strstr(response_buffer, "<resultCode>INFO-00</resultCode>") ||
                   strstr(response_buffer, "NORMAL SERVICE")) {
            write_log("INFO", "후보자 정보 API 요청 완료");
            printf("✅ 후보자 API 응답 수신 완료 (%zu bytes)\n", strlen(response_buffer));
            printf("🎉 실제 API 데이터 사용!\n");
            // 실제 XML 데이터를 그대로 사용 - 변환하지 않음
            free(encoded_key);
            return 0;  // 성공 반환 추가!
        } else if (strstr(response_buffer, "INFO-03") || 
                   strstr(response_buffer, "데이터 정보가 없습니다")) {
            printf("⚠️ 후보자 데이터 없음 (선거ID: %s)\n", election_id);
            free(encoded_key);
            return -1;  // 더미 데이터 생성하지 않고 실패 처리
        } else {
            printf("❓ 알 수 없는 API 응답 (선거ID: %s)\n", election_id);
            free(encoded_key);
            return -1;  // 더미 데이터 생성하지 않고 실패 처리
        }
    } else {
        write_error_log("api_get_candidate_info", "후보자 정보 API 요청 실패");
        printf("❌ 후보자 API 요청 실패 (선거ID: %s)\n", election_id);
        free(encoded_key);
        return -1;  // 더미 데이터 생성하지 않고 실패 처리
    }
}

// 공약 정보 조회 함수 수정 (후보자 ID 필요)
int api_get_pledge_info(APIClient* client, const char* election_id, const char* candidate_id, char* response_buffer, size_t buffer_size) {
    if (!client || !election_id || !candidate_id || !response_buffer) {
        write_error_log("api_get_pledge_info", "잘못된 매개변수");
        return -1;
    }

    // API 키 URL 인코딩
    char* encoded_key = url_encode(client->api_key);
    if (!encoded_key) {
        write_error_log("api_get_pledge_info", "API 키 인코딩 실패");
        return -1;
    }

    // URL 생성 (공약 정보 API 사용) - 최대 100개 요청 (numOfRows=100)
    char url[2048];
    snprintf(url, sizeof(url), 
        "http://apis.data.go.kr%s?serviceKey=%s&pageNo=1&numOfRows=100&sgId=%s&sgTypecode=1&cnddtId=%s",
        PLEDGE_INFO_ENDPOINT, encoded_key, election_id, candidate_id);

    write_log("INFO", "공약 정보 API 요청 시작");
    printf("🌐 공약 API 호출 중 (최대 100개): %s\n", url);

    int result = http_request(url, response_buffer, buffer_size);
    
    free(encoded_key);
    
    if (result == 0) {
        write_log("INFO", "공약 정보 API 요청 완료");
        printf("✅ 공약 API 응답 수신 완료 (%zu bytes)\n", strlen(response_buffer));
    } else {
        write_error_log("api_get_pledge_info", "공약 정보 API 요청 실패");
        printf("❌ 공약 API 요청 실패\n");
    }

    return result;
}

// 간단한 JSON 파싱 구현
int parse_election_json(const char* json_data, ElectionInfo elections[], int max_elections) {
    if (!json_data || !elections) return 0;
    
    write_log("INFO", "선거 정보 파싱 중...");
    
    // JSON 응답 확인
    printf("📄 API 응답 데이터:\n%s\n\n", json_data);
    
    int count = 0;
    
    // XML 응답 확인 (API가 XML로 응답함)
    if (strstr(json_data, "<resultCode>INFO-00</resultCode>") || 
        strstr(json_data, "NORMAL SERVICE")) {
        printf("✅ API 응답 성공 (XML 형식)\n");
        
        // XML에서 선거 정보 추출
        char* item_start = strstr(json_data, "<item>");
        while (item_start && count < max_elections) {
            char* item_end = strstr(item_start, "</item>");
            if (!item_end) break;
            
            // sgId 추출
            char* sgid_start = strstr(item_start, "<sgId>");
            char* sgid_end = strstr(item_start, "</sgId>");
            if (sgid_start && sgid_end && sgid_start < item_end) {
                sgid_start += 6; // "<sgId>" 길이
                int len = sgid_end - sgid_start;
                if (len < MAX_STRING_LEN) {
                    strncpy(elections[count].election_id, sgid_start, len);
                    elections[count].election_id[len] = '\0';
                    
                    // ⭐ 2008년 이전 선거 데이터 필터링 (공약 제출 제도 이전) ⭐
                    int election_year = atoi(elections[count].election_id) / 10000; // YYYYMMDD에서 YYYY 추출
                    if (election_year < 2008) {
                        printf("   ⚠️  %d년 선거 데이터 제외 (공약 제출 제도 이전)\n", election_year);
                        // 다음 item으로 건너뛰기
                        item_start = strstr(item_end, "<item>");
                        continue;
                    }
                }
            }
            
            // sgName 추출
            char* name_start = strstr(item_start, "<sgName>");
            char* name_end = strstr(item_start, "</sgName>");
            if (name_start && name_end && name_start < item_end) {
                name_start += 8; // "<sgName>" 길이
                int len = name_end - name_start;
                if (len < MAX_STRING_LEN) {
                    strncpy(elections[count].election_name, name_start, len);
                    elections[count].election_name[len] = '\0';
                }
            }
            
            // sgVotedate 추출
            char* date_start = strstr(item_start, "<sgVotedate>");
            char* date_end = strstr(item_start, "</sgVotedate>");
            if (date_start && date_end && date_start < item_end) {
                date_start += 12; // "<sgVotedate>" 길이
                int len = date_end - date_start;
                if (len == 8) { // YYYYMMDD 형식
                    char temp[16];
                    strncpy(temp, date_start, 8);
                    temp[8] = '\0';
                    // YYYY-MM-DD 형식으로 변환
                    snprintf(elections[count].election_date, MAX_STRING_LEN, 
                            "%.4s-%.2s-%.2s", temp, temp+4, temp+6);
                }
            }
            
            // sgTypecode 추출 - 이게 핵심!
            char* typecode_start = strstr(item_start, "<sgTypecode>");
            char* typecode_end = strstr(item_start, "</sgTypecode>");
            if (typecode_start && typecode_end && typecode_start < item_end) {
                typecode_start += 12; // "<sgTypecode>" 길이
                int len = typecode_end - typecode_start;
                if (len < 10) {
                    char typecode_str[10];
                    strncpy(typecode_str, typecode_start, len);
                    typecode_str[len] = '\0';
                    int typecode = atoi(typecode_str);
                    
                    // ⭐ 대통령선거만 필터링 ⭐
                    // sgTypecode: 1=대통령선거만 허용
                    if (typecode != 1) {
                        printf("   ⚠️  선거 %s (타입코드: %d) - 대통령선거 아님 (건너뛰기)\n", 
                               elections[count].election_id, typecode);
                        // 다음 item으로 건너뛰기
                        item_start = strstr(item_end, "<item>");
                        continue;
                    }
                    
                    elections[count].is_active = typecode;  // sgTypecode를 is_active 필드에 저장
                    printf("   ✅ 선거 %s (타입코드: %d) - 대통령선거 선택\n", 
                           elections[count].election_id, typecode);
                }
            }
            
            strcpy(elections[count].election_type, "선거");
            count++;
            
            // 다음 item 찾기
            item_start = strstr(item_end, "<item>");
        }
        
        printf("🔍 실제 선거 데이터 %d개 파싱 완료!\n", count);
    } else {
        printf("❌ API 오류 응답\n");
        // 오류 메시지 추출
        char* error_msg = strstr(json_data, "<resultMsg>");
        if (error_msg) {
            char* error_end = strstr(error_msg, "</resultMsg>");
            if (error_end) {
                printf("오류 내용: %.*s\n", (int)(error_end - error_msg - 11), error_msg + 11);
            }
        }
    }
    
    write_log("INFO", "선거 정보 파싱 완료");
    return count;
}

int parse_candidate_json(const char* json_data, const char* election_id, CandidateInfo candidates[], int max_candidates) {
    if (!json_data || !election_id || !candidates) return 0;
    
    write_log("INFO", "후보자 정보 JSON 파싱 중...");
    
    int count = 0;
    

    
    // XML 또는 JSON 응답 확인
    if (strstr(json_data, "<resultCode>INFO-00</resultCode>") || 
        strstr(json_data, "\"resultCode\":\"00\"") || 
        strstr(json_data, "NORMAL SERVICE")) {
        printf("✅ 후보자 API 응답 성공\n");
        
        // XML 형식인지 확인
        if (strstr(json_data, "<items>")) {
            printf("📄 XML 형식 응답 파싱 중...\n");
            
            // XML에서 후보자 정보 추출
            char* items_start = strstr(json_data, "<items>");
            char* items_end = strstr(json_data, "</items>");
            
            // </items> 태그가 없으면 전체 응답 끝까지 파싱
            if (!items_end) {
                items_end = json_data + strlen(json_data);
                printf("🔍 디버깅: </items> 태그 없음, 전체 응답 파싱\n");
            }
            
            if (items_start) {
                char* pos = items_start;
                printf("🔍 디버깅: XML 파싱 시작 (길이: %zu bytes)\n", strlen(json_data));
                
                // 각 <item> 태그 파싱
                while ((pos = strstr(pos, "<item>")) != NULL && count < max_candidates) {
                    char* item_end = strstr(pos, "</item>");
                    if (!item_end || item_end > items_end) break;
                    
                    printf("🔍 디버깅: item %d 파싱 중...\n", count + 1);
                    
                    // name 추출
                    char* name_start = strstr(pos, "<name>");
                    if (name_start && name_start < item_end) {
                        name_start += 6; // "<name>" 길이
                        char* name_end = strstr(name_start, "</name>");
                        if (name_end && name_end < item_end) {
                            int len = name_end - name_start;
                            if (len < MAX_STRING_LEN) {
                                strncpy(candidates[count].candidate_name, name_start, len);
                                candidates[count].candidate_name[len] = '\0';
                            }
                        }
                    }
                    
                    // jdName (정당명) 추출
                    char* party_start = strstr(pos, "<jdName>");
                    if (party_start && party_start < item_end) {
                        party_start += 8; // "<jdName>" 길이
                        char* party_end = strstr(party_start, "</jdName>");
                        if (party_end && party_end < item_end) {
                            int len = party_end - party_start;
                            if (len < MAX_STRING_LEN) {
                                strncpy(candidates[count].party_name, party_start, len);
                                candidates[count].party_name[len] = '\0';
                            }
                        }
                    }
                    
                    // huboid (후보자ID) 추출
                    char* id_start = strstr(pos, "<huboid>");
                    if (id_start && id_start < item_end) {
                        id_start += 8; // "<huboid>" 길이
                        char* id_end = strstr(id_start, "</huboid>");
                        if (id_end && id_end < item_end) {
                            int len = id_end - id_start;
                            if (len < MAX_STRING_LEN) {
                                strncpy(candidates[count].candidate_id, id_start, len);
                                candidates[count].candidate_id[len] = '\0';
                            }
                        }
                    }
                    
                    // giho (후보자 번호) 추출
                    char* num_start = strstr(pos, "<giho>");
                    if (num_start && num_start < item_end) {
                        num_start += 6; // "<giho>" 길이
                        char* num_end = strstr(num_start, "</giho>");
                        if (num_end && num_end < item_end) {
                            char num_str[10];
                            int len = num_end - num_start;
                            if (len < 10) {
                                strncpy(num_str, num_start, len);
                                num_str[len] = '\0';
                                candidates[count].candidate_number = atoi(num_str);
                            }
                        }
                    }
                    
                    // 기본값 설정
                    strcpy(candidates[count].election_id, election_id);
                    candidates[count].pledge_count = 3 + (count % 5);
                    
                    printf("   %d. %s (%s) - 번호: %d, ID: %s\n", 
                           count + 1,
                           candidates[count].candidate_name,
                           candidates[count].party_name,
                           candidates[count].candidate_number,
                           candidates[count].candidate_id);
                    
                    count++;
                    pos = item_end + 1;
                }
                printf("🔍 디버깅: XML 파싱 완료, 총 %d명 파싱\n", count);
            } else {
                printf("🔍 디버깅: <items> 태그를 찾을 수 없음\n");
            }
        }
        // JSON에서 후보자 정보 추출
        else if (strstr(json_data, "\"items\":[")) {
            printf("📄 JSON 형식 응답 파싱 중...\n");
            char* items_start = strstr(json_data, "\"items\":[");
            if (items_start) {
            char* pos = items_start;
            
            // 각 후보자 정보 파싱
            while ((pos = strstr(pos, "{")) != NULL && count < max_candidates) {
                char* item_end = strstr(pos, "}");
                if (!item_end) break;
                
                // name 추출
                char* name_start = strstr(pos, "\"name\":\"");
                if (name_start && name_start < item_end) {
                    name_start += 8; // "name":" 길이
                    char* name_end = strstr(name_start, "\"");
                    if (name_end && name_end < item_end) {
                        int len = name_end - name_start;
                        if (len < MAX_STRING_LEN) {
                            strncpy(candidates[count].candidate_name, name_start, len);
                            candidates[count].candidate_name[len] = '\0';
                        }
                    }
                }
                
                // jdName (정당명) 추출
                char* party_start = strstr(pos, "\"jdName\":\"");
                if (party_start && party_start < item_end) {
                    party_start += 10; // "jdName":" 길이
                    char* party_end = strstr(party_start, "\"");
                    if (party_end && party_end < item_end) {
                        int len = party_end - party_start;
                        if (len < MAX_STRING_LEN) {
                            strncpy(candidates[count].party_name, party_start, len);
                            candidates[count].party_name[len] = '\0';
                        }
                    }
                }
                
                // cnddtId (후보자ID) 추출
                char* id_start = strstr(pos, "\"cnddtId\":\"");
                if (id_start && id_start < item_end) {
                    id_start += 11; // "cnddtId":" 길이
                    char* id_end = strstr(id_start, "\"");
                    if (id_end && id_end < item_end) {
                        int len = id_end - id_start;
                        if (len < MAX_STRING_LEN) {
                            strncpy(candidates[count].candidate_id, id_start, len);
                            candidates[count].candidate_id[len] = '\0';
                        }
                    }
                }
                
                // num (후보자 번호) 추출
                char* num_start = strstr(pos, "\"num\":\"");
                if (num_start && num_start < item_end) {
                    num_start += 7; // "num":" 길이
                    char* num_end = strstr(num_start, "\"");
                    if (num_end && num_end < item_end) {
                        char num_str[10];
                        int len = num_end - num_start;
                        if (len < 10) {
                            strncpy(num_str, num_start, len);
                            num_str[len] = '\0';
                            candidates[count].candidate_number = atoi(num_str);
                        }
                    }
                }
                
                // 기본값 설정 - 실제 선거ID 사용
                strcpy(candidates[count].election_id, election_id); // 실제 선거ID 사용
                candidates[count].pledge_count = 3 + (count % 5); // 3~7개 랜덤
                
                printf("   %d. %s (%s) - 번호: %d\n", 
                       count + 1,
                       candidates[count].candidate_name,
                       candidates[count].party_name,
                       candidates[count].candidate_number);
                
                count++;
                pos = item_end + 1;
            }
            }
        }
        
        if (count == 0) {
            printf("⚠️  파싱된 후보자 데이터가 없습니다\n");
        }
    } else {
        printf("❌ 후보자 API 오류 응답\n");
        char* error_msg = strstr(json_data, "\"resultMsg\":");
        if (error_msg) {
            printf("오류 내용: %.100s\n", error_msg);
        }
    }
    
    write_log("INFO", "후보자 정보 파싱 완료");
    printf("📊 총 %d명의 후보자 정보 파싱 완료\n", count);
    return count;
}

int parse_pledge_json(const char* json_data, PledgeInfo pledges[], int max_pledges) {
    if (!json_data || !pledges) return 0;
    
    write_log("INFO", "공약 정보 XML 파싱 중...");
    
    int count = 0;
    
    // XML 응답 확인
    if (strstr(json_data, "<resultCode>INFO-00</resultCode>") || 
        strstr(json_data, "NORMAL SERVICE")) {
        printf("✅ 공약 API 응답 성공\n");
        printf("📄 XML 형식 공약 응답 파싱 중...\n");
        
        // XML에서 공약 정보 추출
        char* item_start = strstr(json_data, "<item>");
        if (item_start) {
            printf("🔍 <item> 태그 발견!\n");
            char* item_end = strstr(item_start, "</item>");
            if (!item_end) return 0;
            
            // 후보자 기본 정보 추출
            char candidate_id[MAX_STRING_LEN] = {0};
            char candidate_name[MAX_STRING_LEN] = {0};
            
            // cnddtId 추출
            printf("🔍 <cnddtId> 태그 검색 중...\n");
            char* cnddtid_start = strstr(item_start, "<cnddtId>");
            if (cnddtid_start && cnddtid_start < item_end) {
                printf("🔍 <cnddtId> 태그 발견!\n");
                cnddtid_start += 9; // "<cnddtId>" 길이
                char* cnddtid_end = strstr(cnddtid_start, "</cnddtId>");
                if (cnddtid_end && cnddtid_end < item_end) {
                    int len = cnddtid_end - cnddtid_start;
                    if (len < MAX_STRING_LEN) {
                        strncpy(candidate_id, cnddtid_start, len);
                        candidate_id[len] = '\0';
                        printf("🔍 후보자 ID 추출 성공: '%s'\n", candidate_id);
                    } else {
                        printf("❌ 후보자 ID 길이 초과: %d\n", len);
                    }
                } else {
                    printf("❌ </cnddtId> 태그를 찾을 수 없음\n");
                }
            } else {
                printf("❌ <cnddtId> 태그를 찾을 수 없음\n");
            }
            
            // krName 추출
            char* name_start = strstr(item_start, "<krName>");
            if (name_start && name_start < item_end) {
                name_start += 8; // "<krName>" 길이
                char* name_end = strstr(name_start, "</krName>");
                if (name_end && name_end < item_end) {
                    int len = name_end - name_start;
                    if (len < MAX_STRING_LEN) {
                        strncpy(candidate_name, name_start, len);
                        candidate_name[len] = '\0';
                        printf("🔍 후보자 이름 추출 성공: '%s'\n", candidate_name);
                    } else {
                        printf("❌ 후보자 이름 길이 초과: %d\n", len);
                    }
                } else {
                    printf("❌ </krName> 태그를 찾을 수 없음\n");
                }
            } else {
                printf("❌ <krName> 태그를 찾을 수 없음\n");
            }
            
            printf("🔍 후보자 '%s' (ID: %s')의 공약 파싱 중...\n", candidate_name, candidate_id);
            printf("🔍 후보자 ID 길이: %zu, 이름 길이: %zu\n", strlen(candidate_id), strlen(candidate_name));
            
            if (strlen(candidate_id) == 0) {
                printf("❌ 후보자 ID 추출 실패! XML 내용 일부:\n");
                printf("%.1000s\n", item_start);
                return 0;
            }
            
            if (strlen(candidate_name) == 0) {
                printf("❌ 후보자 이름 추출 실패! XML 내용 일부:\n");
                printf("%.1000s\n", item_start);
                return 0;
            }
            
            // 공약 개수 확인
            int pledge_count = 0;
            char* prmscnt_start = strstr(item_start, "<prmsCnt>");
            if (prmscnt_start && prmscnt_start < item_end) {
                prmscnt_start += 9; // "<prmsCnt>" 길이
                char* prmscnt_end = strstr(prmscnt_start, "</prmsCnt>");
                if (prmscnt_end && prmscnt_end < item_end) {
                    char count_str[10];
                    int len = prmscnt_end - prmscnt_start;
                    if (len < 10) {
                        strncpy(count_str, prmscnt_start, len);
                        count_str[len] = '\0';
                        pledge_count = atoi(count_str);
                    }
                }
            }
            
            printf("📋 총 %d개 공약 발견\n", pledge_count);
            
            if (pledge_count == 0) {
                printf("⚠️  공약 개수가 0입니다. XML 내용 일부 출력:\n");
                printf("%.500s\n", json_data);
            }
            
            // 각 공약 파싱 (최대 10개)
            for (int i = 1; i <= pledge_count && i <= 10 && count < max_pledges; i++) {
                char tag_name[50];
                
                // 공약 제목 추출
                snprintf(tag_name, sizeof(tag_name), "<prmsTitle%d>", i);
                char* title_start = strstr(item_start, tag_name);
                if (title_start && title_start < item_end) {
                    title_start += strlen(tag_name);
                    snprintf(tag_name, sizeof(tag_name), "</prmsTitle%d>", i);
                    char* title_end = strstr(title_start, tag_name);
                    if (title_end && title_end < item_end) {
                        int len = title_end - title_start;
                        if (len < MAX_STRING_LEN) {
                            strncpy(pledges[count].title, title_start, len);
                            pledges[count].title[len] = '\0';
                        }
                    }
                }
                
                // 공약 내용 추출
                snprintf(tag_name, sizeof(tag_name), "<prmmCont%d>", i);
                char* content_start = strstr(item_start, tag_name);
                if (content_start && content_start < item_end) {
                    content_start += strlen(tag_name);
                    snprintf(tag_name, sizeof(tag_name), "</prmmCont%d>", i);
                    char* content_end = strstr(content_start, tag_name);
                    if (content_end && content_end < item_end) {
                        int len = content_end - content_start;
                        if (len < MAX_CONTENT_LEN) {
                            strncpy(pledges[count].content, content_start, len);
                            pledges[count].content[len] = '\0';
                        }
                    }
                }
                
                // 공약 분야 추출
                snprintf(tag_name, sizeof(tag_name), "<prmsRealmName%d>", i);
                char* realm_start = strstr(item_start, tag_name);
                if (realm_start && realm_start < item_end) {
                    realm_start += strlen(tag_name);
                    snprintf(tag_name, sizeof(tag_name), "</prmsRealmName%d>", i);
                    char* realm_end = strstr(realm_start, tag_name);
                    if (realm_end && realm_end < item_end) {
                        int len = realm_end - realm_start;
                        if (len < MAX_STRING_LEN) {
                            strncpy(pledges[count].category, realm_start, len);
                            pledges[count].category[len] = '\0';
                        }
                    }
                }
                
                // 기본 정보 설정
                snprintf(pledges[count].pledge_id, MAX_STRING_LEN, "%s_%d", candidate_id, i);
                strcpy(pledges[count].candidate_id, candidate_id);
                pledges[count].like_count = 0;
                pledges[count].dislike_count = 0;
                pledges[count].created_time = time(NULL);
                
                printf("   %d. [%s] %s\n", i, pledges[count].category, pledges[count].title);
                count++;
            }
        } else {
            printf("❌ <item> 태그를 찾을 수 없습니다. XML 내용 일부 출력:\n");
            printf("%.500s\n", json_data);
        }
        
        printf("🎉 공약 정보 %d개 파싱 완료!\n", count);
    } else {
        printf("❌ 공약 API 오류 응답\n");
        char* error_msg = strstr(json_data, "<resultMsg>");
        if (error_msg) {
            char* error_end = strstr(error_msg, "</resultMsg>");
            if (error_end) {
                printf("오류 내용: %.*s\n", (int)(error_end - error_msg - 11), error_msg + 11);
            }
        }
    }
    
    write_log("INFO", "공약 정보 파싱 완료");
    return count;
}

// API 응답 유효성 검사
int validate_api_response(const char* json_data) {
    if (!json_data) return 0;
    
    // 기본적인 JSON 형식 확인
    if (strstr(json_data, "resultCode") && strstr(json_data, "resultMsg")) {
        return 1;
    }
    
    return 0;
}

// API 오류 출력
void print_api_error(const char* function_name, const char* error_message) {
    printf("❌ API 오류 [%s]: %s\n", function_name, error_message);
    write_error_log(function_name, error_message);
} 