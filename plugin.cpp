#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdexcept>
#include "teamspeak/public_errors.h"
#include "teamspeak/public_errors_rare.h"
#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "teamspeak/clientlib_publicdefinitions.h"
#include "ts3_functions.h"
#include "plugin.h"
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

static struct TS3Functions ts3Functions;

#define _strcpy(dest, destSize, src) { strncpy(dest, src, destSize-1); (dest)[destSize-1] = '\0'; }

#define PLUGIN_API_VERSION 22

#define PATH_BUFSIZE 512
#define COMMAND_BUFSIZE 128
#define INFODATA_BUFSIZE 128
#define SERVERINFO_BUFSIZE 256
#define CHANNELINFO_BUFSIZE 512
#define RETURNCODE_BUFSIZE 128

#define PLUGIN_NAME "BlinkBot"
#define PLUGIN_AUTHOR "blink;"
#define PLUGIN_VERSION "1.0"
#define PLUGIN_DESCRIPTION "TS3 Music Bot"

#define FFMPEG_ARGS "ffmpeg -f alsa default -thread_queue_size 2048 -i \""



static char* pluginID = NULL;

const char* ts3plugin_name() {
	return PLUGIN_NAME;
}

const char* ts3plugin_version() {
	return "1";
}

int ts3plugin_apiVersion() {
	return PLUGIN_API_VERSION;
}

const char* ts3plugin_author() {
	return PLUGIN_AUTHOR;
}

const char* ts3plugin_description() {
	return PLUGIN_DESCRIPTION;
}

void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
	ts3Functions = funcs;
}

int ts3plugin_init() {
	printf("PLUGIN: init\n");
	return 0;
}

/* Custom code called right before the plugin is unloaded */
void ts3plugin_shutdown() {
	/* Your plugin cleanup code here */
	printf("PLUGIN: shutdown\n");

	/*
	 * Note:
	 * If your plugin implements a settings dialog, it must be closed and deleted here, else the
	 * TeamSpeak client will most likely crash (DLL removed but dialog from DLL code still open).
	 */

	 /* Free pluginID if we registered it */
	if (pluginID) {
		free(pluginID);
		pluginID = NULL;
	}
}

void ts3plugin_freeMemory(void* data) {
	free(data);
}

int ts3plugin_requestAutoload() {
	return 1;  /* 1 = request autoloaded, 0 = do not request autoload */
}

int ts3plugin_offersConfigure() {
	printf("PLUGIN: offersConfigure\n");
	/*
	 * Return values:
	 * PLUGIN_OFFERS_NO_CONFIGURE         - Plugin does not implement ts3plugin_configure
	 * PLUGIN_OFFERS_CONFIGURE_NEW_THREAD - Plugin does implement ts3plugin_configure and requests to run this function in an own thread
	 * PLUGIN_OFFERS_CONFIGURE_QT_THREAD  - Plugin does implement ts3plugin_configure and requests to run this function in the Qt GUI thread
	 */
	return PLUGIN_OFFERS_NO_CONFIGURE;
}

void ts3plugin_configure(void* handle, void* qParentWidget) {
	printf("PLUGIN: configure\n");
}

void ts3plugin_registerPluginID(const char* id) {
	const size_t sz = strlen(id) + 1;
	pluginID = (char*)malloc(sz * sizeof(char));
	_strcpy(pluginID, sz, id); /* The id buffer will invalidate after exiting this function */
	printf("PLUGIN: registerPluginID: %s\n", pluginID);
}

string exec_get_out(const char* cmd) {
	char buffer[128];
	std::string result = "";
	FILE* pipe = popen(cmd, "r");
	if (!pipe) throw std::runtime_error("popen() failed!");
	try {
		while (fgets(buffer, sizeof buffer, pipe) != NULL) {
			result += buffer;
		}
	}
	catch (...) {
		pclose(pipe);
		throw;
	}
	pclose(pipe);

	return result;
}

anyID wIDs[1024];

vector<string> playhistory;

int wIDs_length = 0;

bool IsWhispEnabled = false;

string youtube_id;
string youtube_title;

string currently_playing = "";

int ts3plugin_onTextMessageEvent(uint64 serverConnectionHandlerID, anyID targetMode, anyID toID, anyID fromID, const char* fromName, const char*
	fromUniqueIdentifier, const char* message, int ffIgnored) {
	printf("PLUGIN: onTextMessageEvent %llu %d %d %s %s %d\n", (long long unsigned int)serverConnectionHandlerID, targetMode, fromID, fromName,
		message, ffIgnored);
	if (targetMode == 3) return 0; /* Ignore if is a server message */
	/* Friend/Foe manager has ignored the message, so ignore here as well. */
	if (ffIgnored) {
		return 0; /* Client will ignore the message anyways, so return value here doesn't matter */
	}
	anyID myID;
	if (ts3Functions.getClientID(serverConnectionHandlerID, &myID) != ERROR_ok) {
		ts3Functions.logMessage("Error querying own client id", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
		return 0;
	}
	if (fromID != myID) {
		if (strcasecmp(message, "!whisp off") == 0 || strcmp(message, "!whisper off") == 0 || strcmp(message, "!w off") == 0) {
			if (ts3Functions.requestClientSetWhisperList(serverConnectionHandlerID, 0, NULL, NULL, 0) != ERROR_ok) {
				ts3Functions.logMessage("Error trying to whisper", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			}
			IsWhispEnabled = false;
		}
		if (strcasecmp(message, "!whisp on") == 0 || strcmp(message, "!whisper on") == 0 || strcmp(message, "!w on") == 0) {
			if (ts3Functions.requestClientSetWhisperList(serverConnectionHandlerID, 0, NULL, wIDs, 0) != ERROR_ok) {
				ts3Functions.logMessage("Error trying to whisper", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
			}
			IsWhispEnabled = true;
		}
		if (strcasecmp(message, "!sub") == 0) {
			if (wIDs_length > 0) {
				for (int f = 0; f < wIDs_length; f++) {
					if (wIDs[f] == fromID) {
						if (IsWhispEnabled) {
							if (ts3Functions.requestClientSetWhisperList(serverConnectionHandlerID, 0, NULL, wIDs, 0) != ERROR_ok) {
								ts3Functions.logMessage("Error trying to whisper", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
							}
						}
						return 0;
					}
				}
			}

			if (wIDs_length < 1024) {
				wIDs[wIDs_length++] = fromID;
			}
			if (IsWhispEnabled) {
				if (ts3Functions.requestClientSetWhisperList(serverConnectionHandlerID, 0, NULL, wIDs, 0) != ERROR_ok) {
					ts3Functions.logMessage("Error trying to whisper", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
				}
			}
		}
		if (strcasecmp(message, "!unsub") == 0) {
			if (wIDs_length == 0) return 0;
			if (wIDs_length == 1) {
				wIDs[0] = 0;
				wIDs_length = 0;
				if (ts3Functions.requestClientSetWhisperList(serverConnectionHandlerID, 0, NULL, wIDs, 0) != ERROR_ok) {
					ts3Functions.logMessage("Error trying to whisper", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
				}
				return 0;
			}
			for (int f = 0; f < wIDs_length; f++) {
				if (wIDs[f] == fromID) {
					wIDs[f] = wIDs[wIDs_length - 1];
					wIDs[wIDs_length - 1] = 0;
					wIDs_length--;
					if (IsWhispEnabled) {
						if (ts3Functions.requestClientSetWhisperList(serverConnectionHandlerID, 0, NULL, wIDs, 0) != ERROR_ok) {
							ts3Functions.logMessage("Error trying to whisper", LogLevel_ERROR, "Plugin", serverConnectionHandlerID);
						}
					}
					return 0;
				}
			}
		}
		if (strcasestr(message, "!play")) {
			try {
				string msg = message;
				string link = msg.substr(msg.find("[URL]") + 5, msg.find("[/URL]") - (msg.find("[URL]") + 5));
				string cmdout = link;
				youtube_id = "";
				youtube_title = "";
				currently_playing = cmdout;
				if (strstr(link.c_str(), "youtube.com/") || strstr(link.c_str(), "youtu.be/")) {
					string cmdin = "youtube-dl -g -f bestaudio \"" + link + "\" -e";
					cmdout = exec_get_out(cmdin.c_str());

					istringstream f(cmdout);
					string line;
					unsigned int line_number(1);
					while (getline(f, line)) {
						if (line_number == 1) youtube_title = line;
						if (line_number == 2) cmdout = line;
						line_number++;
					}
					cmdout.erase(cmdout.length() - 1);
				}
				if (youtube_title != "") playhistory.push_back("[URL=" + link + "]" + youtube_title + "[/URL]"); else
					playhistory.push_back("[URL]" + link + "[/URL]");
				string cmdline = FFMPEG_ARGS + cmdout + "\" > /dev/null 2>&1 &";
				system("pkill -f ffmpeg");
				system(cmdline.c_str());
			}
			catch (...) { ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Some error happened!", fromID, NULL); }
		}
		if (strcasecmp(message, "!stop") == 0) {
			system("pkill -f ffmpeg");
			currently_playing = "";
		}
		if (strcasestr(message, "!vol")) {
			string msg = message;
			string vol = msg.substr(msg.find("vol ") + 4);
			string cmdin = "amixer set PCM " + vol + "%";
			string cmdout = exec_get_out(cmdin.c_str());
		}
		if (strcasestr(message, "!volume")) {
			string msg = message;
			string vol = msg.substr(msg.find("volume ") + 7);
			string cmdin = "amixer set PCM " + vol + "%";
			string cmdout = exec_get_out(cmdin.c_str());
		}
		if (strcasestr(message, "!ytsearch")) {
			try {
				string msg = message;
				string searchquery = msg.substr(msg.find("!ytsearch ") + 10);
				string cmdin = "youtube-dl \"ytsearch:" + searchquery + "\" -g -f bestaudio -e --get-id";
				string cmdout = exec_get_out(cmdin.c_str());
				if (strcasecmp(cmdout.c_str(),"") == 0) {
					ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "No results found", fromID, NULL);
					return 0;
				}

				istringstream f(cmdout);
				string line;
				unsigned int line_number(1);
				while (getline(f, line)) {
					if (line_number == 1) youtube_title = line;
					if (line_number == 2) youtube_id = line;
					if (line_number == 3) cmdout = line;
					line_number++;
				}

				cmdout.erase(cmdout.length() - 1);
				
				string cmdline = FFMPEG_ARGS + cmdout + "\" > /dev/null 2>&1 &";
				system("pkill -f ffmpeg");
				system(cmdline.c_str());
				currently_playing = "https://www.youtube.com/watch?v=" + youtube_id;
				//playhistory.push_back("https://www.youtube.com/watch?v=" + youtube_id);
				playhistory.push_back("[URL=https://www.youtube.com/watch?v=" + youtube_id + "]" + youtube_title + "[/URL]");
			}
			catch (...) { ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Some error happened!", fromID, NULL); }
			
		}

		if (strcasecmp(message, "!link") == 0) {
			try {
				if (currently_playing != "") {
					string msgtosend;
					if(youtube_title != "") msgtosend = "[URL=" + currently_playing + "]" + youtube_title + "[/URL]"; else 
						msgtosend = "[URL]" + currently_playing + "[/URL]";
					ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, msgtosend.c_str(), fromID, NULL);
				}
				else
				{
					ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Theres no audio stream currently playing, so theres no link :D", fromID, NULL);
				}
				
			}
			catch(...) { ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Some error happened!", fromID, NULL); }
		}
		if (strcasecmp(message, "!help") == 0) {
			try {
				const char* msg = "[U]Available commands:[/U]\n"
					"[B]!play [link][/B]\n"
					"Used to play provided link (example !play https://www.youtube.com/watch?v=Jc7B-jMcwG4)\n"
					"\n"
					"[B]!stop[/B]\n"
					"Used to stop currently playing audio stream\n"
					"\n"
					"[B]!sub[/B]\n"
					"Used to start whispering the bot if whisper mode is on\n"
					"\n"
					"[B]!unsub[/B]\n"
					"Used to stop whispering the bot if whisper mode is on\n"
					"\n"
					"[B]!whisper [on/off][/B]\n"
					"Used to enabe/disable whisper mode, you can also type !w [on/off]\n"
					"\n"
					"[B]!ytsearch [text][/B]\n"
					"Used to search clip on youtube and play the first result (example !ytsearch dubstep mix)\n"
					"\n"
					"[B]!volume [%][/B]\n"
					"Used to set the volume of the bot, you can also type !vol % (example !vol 50)\n"
					"\n"
					"[B]!link[/B]\n"
					"Used to see current audio stream link\n"
					"\n"
					"[B]!his[/B]\n"
					"Used to see history of played streams\n";
				ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, msg, fromID, NULL);
			}
			catch (...) { ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Some error happened!", fromID, NULL); }
		}
		if (strcasecmp(message, "!his") == 0) {
			try {
				cout << playhistory.size() << endl;
				string msg = "[U]History:[/U]\n";
				for (int i = 0; i < static_cast<int>(playhistory.size()); i++)
				{
					msg += to_string(i) + ". " + playhistory[i] + "\n";
				}
				msg += "Use !his play [index] to play some stream from history (example !his play 1)";
				ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, msg.c_str(), fromID, NULL);
			}
			catch (...) { ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Some error happened!", fromID, NULL); }
		}
		if (strcasestr(message, "!his play")) {
			try {
				string msg = message;
				int playcount = stoi(msg.substr(msg.find("!his play ") + 10));
				if (playcount >= static_cast<int>(playhistory.size())) {
					ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Index by provided number doesnt exist!", fromID, NULL);
					return 0;
				}
				string glink = playhistory[playcount];
				string link = glink.substr(glink.find("[URL]") + 5, glink.find("[/URL]") - (glink.find("[URL]") + 5));
				cout << link << endl;
				string cmdout = link;
				youtube_id = "";
				youtube_title = "";
				currently_playing = cmdout;
				if (strstr(link.c_str(), "youtube.com/") || strstr(link.c_str(), "youtu.be/")) {
					string cmdin = "youtube-dl -g -f bestaudio \"" + link + "\" -e";
					cmdout = exec_get_out(cmdin.c_str());

					istringstream f(cmdout);
					string line;
					unsigned int line_number(1);
					while (getline(f, line)) {
						if (line_number == 1) youtube_title = line;
						if (line_number == 2) cmdout = line;
						line_number++;
					}
					cmdout.erase(cmdout.length() - 1);
				}
				string cmdline = FFMPEG_ARGS + cmdout + "\" > /dev/null 2>&1 &";
				system("pkill -f ffmpeg");
				system(cmdline.c_str());
			}
			catch (...) { ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Some error happened!", fromID, NULL); }

		}

		if (strcasecmp(message, "!exit") == 0) {
			ts3Functions.requestSendPrivateTextMsg(serverConnectionHandlerID, "Goodbye!", fromID, NULL);
			ts3Functions.stopConnection(serverConnectionHandlerID, "Goodbye");
			ts3Functions.destroyServerConnectionHandler(serverConnectionHandlerID);
			system("pkill -f ts3client");
		}

		return 0;
	}
	return 0; /* 0 = handle normally, 1 = client will ignore the text message */
}
