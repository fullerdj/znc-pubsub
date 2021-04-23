#include <algorithm>
#include <string>
#include <iostream>
#include <sstream>
#include <regex>
#include <string.h>

#include <curl/curl.h>

#include <cppcodec/base64_rfc4648.hpp>
// dnf install cppcodec-devel

#include "secrets.h"

size_t write_stdout(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  size_t ret = size * nmemb;
  std::cout.write(ptr, ret);
  return ret;
}

size_t extract_token(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  std::string reply(ptr);
  std::string *token = (std::string *)userdata;
  std::regex re(".*\"access_token\": \"([^\"]*)\",.*", std::regex::extended);
  std::smatch match;

  std::regex_match(reply, match, re);
  
  if (match.size() >= 2) {
    *token = match[1];
  } else {
    *token = "";
  }

  return size*nmemb;
}

std::string auth_message(void)
{
  std::stringstream ss;

  ss << "client_id=" << client_id
     << "&client_secret=" << client_secret
     << "&refresh_token=" << refresh_token
     << "&grant_type=refresh_token";
  return ss.str();
}

std::string pub_message(void)
{
  using base64 = cppcodec::base64_rfc4648;
  std::stringstream ss;
  ss << "{"
     <<   "\"messages\": ["
     <<     "{"
     //<<       "\"attributes\": {},"
     <<       "\"data\": \"" << base64::encode("ughblah") << "\""
     <<     "}"
     <<   "]"
     << "}";
  return ss.str();
}

CURL *do_curl_init(const char *url, const char *postdata) {
  CURL *curl = curl_easy_init();
  if (!curl) {
    std::cerr << "Curl fail" << std::endl;
    return nullptr;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(postdata));

  return curl;
}
  
std::string get_access_token()
{
  std::string token;
  std::string postdata = auth_message();

  CURL *curl = do_curl_init("https://oauth2.googleapis.com/token",
                            postdata.c_str());

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, extract_token);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &token);

  CURLcode success = curl_easy_perform(curl);

  return token;
}

int main (int argc, char **argv)
{
  std::string token = get_access_token();
  if (token == "") {
    std::cerr << "error getting token" << std::endl;
    return -1;
  }

  struct curl_slist *headers = NULL;
  std::string auth_header("Authorization: Bearer ");
              auth_header.append(token.c_str());
  std::string postdata = pub_message();

  CURL *curl = do_curl_init(topic_url, postdata.c_str());

  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "charset: utf-8");
  headers = curl_slist_append(headers, auth_header.c_str());

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_stdout);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode success = curl_easy_perform(curl);

  curl_slist_free_all(headers);

  if (success == CURLE_OK) {
    return EXIT_SUCCESS;
  } else {
    return -1;
  }
}
