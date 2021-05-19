#include <chrono>
#include <regex>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <znc/znc.h>
#include <znc/Chan.h>
#include <znc/User.h>
#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/ZNCString.h>

#include <curl/curl.h>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

const std::string credFile(".creds.json");
const std::string topicFile(".topic.json");

class PubSub: public CModule
{
  public:
  MODCONSTRUCTOR(PubSub) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    user = GetUser();
    last_active = token_expires = std::chrono::system_clock::now();

    struct passwd *pw = getpwuid(getuid());
    std::string homedir(pw->pw_dir);

    rapidjson::Document credDoc = parseJsonFile(homedir + "/" + credFile);
    client_id = credDoc["client_id"].GetString();
    client_secret = credDoc["client_secret"].GetString();
    refresh_token = credDoc["refresh_token"].GetString();

    rapidjson::Document topicDoc = parseJsonFile(homedir + "/" + topicFile);
    topic_url = topicDoc["topic_url"].GetString();
  }

  virtual ~PubSub() {
    curl_global_cleanup();
  }

  protected:
  const CString app = "ZNC";
  CUser *user;
  bool debug = false;

  CString client_id;
  CString client_secret;
  CString refresh_token;
  CString topic_url;

  MCString options;
  MCString defaults;

  CString accessToken;
  std::chrono::time_point<std::chrono::system_clock> last_active;
  std::chrono::time_point<std::chrono::system_clock> token_expires;

  rapidjson::Document parseJsonFile(const std::string &filename) {
    std::ifstream ifs(filename);
    rapidjson::IStreamWrapper isw(ifs);
    rapidjson::Document ret;
    ret.ParseStream(isw);

    return ret;
  }

  CURL *do_curl_init(const char *url, const char *postdata) {
    CURL *curl = curl_easy_init();
    if (!curl) {
      PutModule("Curl fail");
      return nullptr;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(postdata));

    return curl;
  }

  struct tokendata {
    CString token;
    int valid_seconds;
  };
  static size_t extractToken(char *ptr, size_t size, size_t nmemb,
                             void *userdata)
  {
    struct tokendata *td = (struct tokendata *)userdata;
    rapidjson::Document d;
    d.Parse(ptr);
    td->token = d["access_token"].GetString();
    td->valid_seconds = d["expires_in"].GetInt();

    return size*nmemb;
  }

  static size_t getWriteData(char *ptr, size_t size, size_t nmemb,
                             void *userdata)
  {
    CString *reply = (CString *)userdata;

    reply->append(ptr);
    return size*nmemb;
  }

  void checkToken(void)
  {
    if (std::chrono::system_clock::now() < token_expires) {
      return;
    }

    CString postdata;
    postdata += "client_id=";
    postdata += client_id;
    postdata += "&client_secret=";
    postdata += client_secret;
    postdata += "&refresh_token=";
    postdata += refresh_token;
    postdata += "&grant_type=refresh_token";

    CURL *curl = do_curl_init("https://oauth2.googleapis.com/token",
                              postdata.c_str());

    struct tokendata td;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, extractToken);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &td);

    CURLcode success = curl_easy_perform(curl);
    if (success != CURLE_OK) {
      PutModule("Curl fail getting new token");
      return;
    }

    accessToken = td.token;
    token_expires = std::chrono::system_clock::now() +
                    std::chrono::seconds(td.valid_seconds-15);

    if (debug) {
      PutModule("updated token");
    }
  }

  bool matchName(const CString &message)
  {
    CString username = user->ExpandString("%nick%");
    return message.StartsWith(username);
  }

  static CString makeMessage(CString &content)
  {
    rapidjson::Document d;
    d.SetObject();

    auto& allocator = d.GetAllocator();

    rapidjson::Value msgs(rapidjson::kArrayType);
    rapidjson::Value msg(rapidjson::kObjectType);
    rapidjson::Value str(rapidjson::kObjectType);

    content.Base64Encode();
    str.SetString(content.c_str(),
                  static_cast<rapidjson::SizeType>(content.length()),
                  allocator);
    msg.AddMember("data", str, allocator);
    msgs.PushBack(msg, allocator);
    d.AddMember("messages", msgs, allocator);

    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return CString(buf.GetString());
  }

  void publish(CString &content)
  {
    checkToken();
    CString postdata = makeMessage(content);

    struct curl_slist *headers = NULL;
    CString auth_header("Authorization: Bearer ");
            auth_header.append(accessToken.c_str());

    CURL *curl = do_curl_init(topic_url.c_str(), postdata.c_str());
    CString reply;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "charset: utf-8");
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getWriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode success = curl_easy_perform(curl);
    if (success != CURLE_OK) {
      PutModule("Curl fail posting message");
    }

    curl_slist_free_all(headers);

    if (debug)
      PutModule(reply);
  }

  void publish(CTextMessage &message)
  {
    CString content;

    content = message.GetNick().GetNick() + ": " + message.GetText() + " (";
    content += message.GetNetwork()->GetName() + "/";

    CChan *chan = message.GetChan();
    if (chan)
      content += "#" + chan->GetName() + ")";
    else
      content += "direct)";
    publish(content);
  }

  bool OnLoad(const CString &args, CString &message)
  {
    PutModule("hello");
    return true;
  }

  EModRet OnChanTextMessage(CTextMessage &message)
  {
    if (matchName(message.GetText())) {
      publish(message);
    }
    return CONTINUE;
  }

  EModRet OnPrivTextMessage(CTextMessage &message)
  {
    publish(message);

    return CONTINUE;
  }

  void OnModCommand(const CString &command)
  {
    VCString words;

    command.Split(" ", words, false);
    if (words.size() < 2)
      return;

    CString cmd = words[0].AsLower();

    if (cmd == "send") {
      publish(words[1]);
      PutModule("published: " + words[1]);
    } else if (cmd == "debug") {
      if (words[1] == "on") {
        debug = true;
      } else if (words[1] == "off") {
        debug = false;
      } else {
        debug = !debug;
      }
      PutModule("debug " + debug ? "on" : "off");
    }
  }
};

template<> void TModInfo<PubSub>(CModInfo &Info) {
  Info.AddType(CModInfo::UserModule);
  Info.SetWikiPage("PubSub");
}

NETWORKMODULEDEFS(PubSub, "Use Google Cloud PubSub to notify about new messages")
