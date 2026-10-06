#pragma once

#include <string>
#include <vector>

namespace zb {

struct FetchResult {
    bool ok = false;
    int status = 0;
    std::string html;         
    std::string final_url;
    std::string content_type;
    std::string error;
    
    std::vector<std::string> set_cookie;
};




bool FetchUrl(const std::string& url, FetchResult* result, int timeout_ms = 15000);


bool FetchBinary(const std::string& url, FetchResult* result,
                 int timeout_ms = 30000);





bool FetchUrlWithCookies(const std::string& url, FetchResult* result,
                         int timeout_ms = 15000,
                         const std::string& referer = std::string());
bool FetchBinaryWithCookies(const std::string& url, FetchResult* result,
                            int timeout_ms = 30000,
                            const std::string& referer = std::string());




bool FetchUrlPostWithCookies(const std::string& url, const std::string& body,
                             const std::string& content_type,
                             FetchResult* result, int timeout_ms = 15000,
                             const std::string& referer = std::string());




void FetchManyParallel(const std::vector<std::string>& urls, int threads,
                       std::vector<FetchResult>* out, int timeout_ms = 15000,
                       bool binary = true,
                       const std::string& referer = std::string());


std::vector<std::string> CookieJarDump();



void CookieJarAbsorbText(const std::string& url, const std::string& set_cookie);

}  