#define PLUGIN_EXPORTS
#include <plugin/summary.h>
#include <AiLauncher.hpp>

#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;

static minutes::AiLauncher ai;
static std::string str_generate;

int32_t PLUGIN_CALL plugin_summary_start(const char* src, const char* remoteModel) {
	printf("## %s\n", src);
	printf("### %s\n", (fs::current_path() / src).generic_string().c_str());
	return ai.start((fs::current_path() / src).generic_string(), remoteModel);
}

int32_t PLUGIN_CALL plugin_summary_ready(){
	return ai.ready();
}

void PLUGIN_CALL plugin_summary_cancel() {
	ai.cancel();
}

const char* PLUGIN_CALL plugin_summary_generate(const char* prompt, const char* content) {
	str_generate = ai.generate(prompt, content);
	if (str_generate.empty()) [[unlikely]] { return nullptr; }
	return str_generate.c_str();
}

const char* PLUGIN_CALL plugin_summary_lasterror(){
	return ai.lastError().c_str();
}

int32_t PLUGIN_CALL plugin_summary_stop(){
	ai.stop();
	return 1;
}