#include <algorithm>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string.h>

#include <curl/curl.h>

#include <cppcodec/base64_rfc4648.hpp>
// dnf install cppcodec-devel
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "secrets.h"

const char *credFile = "creds.json";

size_t write_stdout(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  size_t ret = size * nmemb;
  std::cout.write(ptr, ret);
  return ret;
}

size_t extract_token(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  std::string *output = (std::string *)userdata;
  rapidjson::Document d;
  d.Parse(ptr);
  *output = d["access_token"].GetString();

  return size*nmemb;
}

std::string auth_message(void)
{
  std::ifstream ifs(credFile);
  rapidjson::IStreamWrapper isw(ifs);
  rapidjson::Document d;
  d.ParseStream(isw);

  std::stringstream ss;

  ss << "client_id=" << d["client_id"].GetString()
     << "&client_secret=" << d["client_secret"].GetString()
     << "&refresh_token=" << d["refresh_token"].GetString()
     << "&grant_type=refresh_token";
  return ss.str();
}

std::string pub_message(std::string content)
{
  using base64 = cppcodec::base64_rfc4648;
  std::string encoded = base64::encode(content);

  rapidjson::Document d;
  d.SetObject();

  auto& allocator = d.GetAllocator();

  rapidjson::Value msgs(rapidjson::kArrayType);
  rapidjson::Value msg(rapidjson::kObjectType);
  rapidjson::Value str(rapidjson::kObjectType);


  str.SetString(encoded.c_str(),
                static_cast<rapidjson::SizeType>(encoded.length()), allocator);
  msg.AddMember("data", str, allocator);
  msgs.PushBack(msg, allocator);
  d.AddMember("messages", msgs, allocator);

  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
  d.Accept(writer);

  return buf.GetString();
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

  std::string postdata;

  if (argc > 1) {
    postdata = pub_message(std::string(argv[1]));
  } else {
    postdata = pub_message("fooblah");
  }

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
