#include <chrono>
#include <regex>
#include <iostream>

#include <znc/znc.h>
#include <znc/Chan.h>
#include <znc/User.h>
#include <znc/Modules.h>
#include <znc/Client.h>
#include <curl/curl.h>

#include "secrets.h"


class PubSub: public CModule
{
	public:
		MODCONSTRUCTOR(PubSub) {
			curl_global_init(CURL_GLOBAL_DEFAULT);
			user = GetUser();
      last_active = token_expires = std::chrono::system_clock::now();
    }

		virtual ~PubSub() {
			curl_global_cleanup();
		}

	protected:
		const CString app = "ZNC";
		CUser *user;
		MCString options;
		MCString defaults;

    CString accessToken;
    std::chrono::time_point<std::chrono::system_clock> last_active;
    std::chrono::time_point<std::chrono::system_clock> token_expires;

    CURL *do_curl_init(const char *url, const char *postdata) {
      CURL *curl = curl_easy_init();
      if (!curl) {
        PutModule("Curl fail");
        return nullptr;
      }

      curl_easy_setopt(curl, CURLOPT_URL, url);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(postdata));
      std::cout << postdata << " " << strlen(postdata) << std::endl;

      return curl;
    }

    static size_t extractToken(char *ptr, size_t size, size_t nmemb,
                               void *userdata)
    {
      CString reply(ptr);
      CString *token = (CString *)userdata;
      std::regex re(".*\"access_token\": \"([^\"]*)\",.*",
                    std::regex::extended);
      std::smatch match;

      std::regex_match(reply, match, re);

      std::cout << reply << match[1] << std::endl;

      if (match.size() >= 2) {
        *token = CString(match[1]);
      } else {
        *token = "";
      }

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

      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, extractToken);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &accessToken);

      CURLcode success = curl_easy_perform(curl);
      if (success != CURLE_OK) {
        PutModule("Curl fail getting new token");
      } else {
        PutModule("updated token");
        PutModule(accessToken);
      }
    }

    bool matchName(const CString &message)
    {
      CString username = user->ExpandString("%nick%");
      PutModule(message);
      PutModule(username);
      return message.StartsWith(username);
    }

    void publish(CTextMessage &message)
    {
      checkToken();

      CString content;
      CString postdata;

      content = message.GetNick().GetNick() + ": " + message.GetText() + " ("
              + message.GetChan()->GetName() + ")";

      content.Base64Encode();

      postdata += "{";
      postdata +=  "\"messages\": [";
      postdata +=    "{";
      //postdata +=      "\"attributes\": {},";
      postdata +=      "\"data\": \"" + content + "\"";
      postdata +=    "}";
      postdata +=  "]";
      postdata +="}";

      struct curl_slist *headers = NULL;
      CString auth_header("Authorization: Bearer ");
              auth_header.append(accessToken.c_str());

      CURL *curl = do_curl_init(topic_url, postdata.c_str());
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

      PutModule(reply);
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

		void OnModCommand(const CString& command)
		{
      PutModule(command);
    }
/*
		EModRet OnChanAction(CNick& nick, CChan& channel, CString& message)
		{
			if (notify_channel(nick, channel, message))
			{
				CString title = "Highlight";

				send_message(message, title, channel.GetName(), nick);
			}

			return CONTINUE;
		}

		EModRet OnChanNotice(CNick& nick, CChan& channel, CString& message)
		{
			if (notify_channel(nick, channel, message))
			{
				CString title = "Channel Notice";

				send_message(message, title, channel.GetName(), nick);
			}

			return CONTINUE;
		}

		EModRet OnPrivMsg(CNick& nick, CString& message)
		{
			if (notify_pm(nick, message))
			{
				CString title = "Private Message";

				send_message(message, title, nick.GetNick(), nick);
			}

			return CONTINUE;
		}

		EModRet OnPrivAction(CNick& nick, CString& message)
		{
			if (notify_pm(nick, message))
			{
				CString title = "Private Message";

				send_message(message, title, nick.GetNick(), nick);
			}

			return CONTINUE;
		}

		EModRet OnPrivNotice(CNick& nick, CString& message)
		{
			if (notify_pm(nick, message))
			{
				CString title = "Private Notice";

				send_message(message, title, nick.GetNick(), nick);
			}

			return CONTINUE;
		}

		EModRet OnUserMsg(CString& target, CString& message)
		{
			last_reply_time[target] = last_active_time[target] = idle_time = time(NULL);
			return CONTINUE;
		}

		EModRet OnUserAction(CString& target, CString& message)
		{
			last_reply_time[target] = last_active_time[target] = idle_time = time(NULL);
			return CONTINUE;
		}

		EModRet OnUserNotice(CString& target, CString& message)
		{
			last_reply_time[target] = last_active_time[target] = idle_time = time(NULL);
			return CONTINUE;
		}

		EModRet OnUserJoin(CString& channel, CString& key)
		{
			idle_time = time(NULL);
			return CONTINUE;
		}

		EModRet OnUserPart(CString& channel, CString& message)
		{
			idle_time = time(NULL);
			return CONTINUE;
		}

		EModRet OnUserTopic(CString& channel, CString& topic)
		{
			idle_time = time(NULL);
			return CONTINUE;
		}

		EModRet OnUserTopicRequest(CString& channel)
		{
			idle_time = time(NULL);
			return CONTINUE;
		}

*/
};

template<> void TModInfo<PubSub>(CModInfo& Info) {
	Info.AddType(CModInfo::UserModule);
	Info.SetWikiPage("PubSub");
}

NETWORKMODULEDEFS(PubSub, "Use Google Cloud PubSub to notify about new messages")
