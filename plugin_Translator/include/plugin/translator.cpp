#define PLUGIN_EXPORTS
#include <plugin/translator.h>
#include <OfflineTranslator.hpp>

#include <cassert>
#include <filesystem>
#include <string>
#include <mutex>

namespace fs = std::filesystem;

using offline_translator::Translator;
using offline_translator::Result;

static Translator translator{};
static std::string register_error{};
static std::string unload_error{};
static std::string translate_out{};
static std::string translate_error{};
static std::mutex mtx;

plugin_translator_res PLUGIN_CALL 
plugin_translator_register(const char* srcLanguage, const char* tarLanguage, const char* modelDir, char** error) {
	std::lock_guard lock{ mtx };
	bool ok = translator.registerModel(srcLanguage, tarLanguage, fs::path{ modelDir }, &register_error);
	if (!ok) [[unlikely]] {
		*error = register_error.data();
		return plugin_translator_res_error;
	}
	translator.setDefaultLanguages(srcLanguage, tarLanguage);
	return plugin_translator_res_ok;
}

plugin_translator_res PLUGIN_CALL
plugin_translator_unload(const char* srcLanguage, const char* tarLanguage, char** error) {
	std::lock_guard lock{ mtx };
	bool ok = translator.unloadModel(srcLanguage, tarLanguage, &unload_error);
	if (!ok) {
		*error = unload_error.data();
		return plugin_translator_res_error;
	}
	return plugin_translator_res_ok;
}

plugin_translator_res PLUGIN_CALL 
plugin_translator_translate(const char* text, const char* srcLanguage, const char* tarLanguage, char** out, char** error) {
	std::lock_guard lock{ mtx };
	auto res = translator.translate(text, srcLanguage, tarLanguage);
	if (!res.ok) [[unlikely]] {
		translate_error = res.error;
		*error = translate_error.data();
		return plugin_translator_res_error;
	}
	translate_out = res.text;
	*out = translate_out.data();
	return plugin_translator_res_ok;
}

void PLUGIN_CALL 
plugin_translator_close() {
	std::lock_guard lock{ mtx };
	translator = {};
	register_error.clear();
	unload_error.clear();
	translate_out.clear();
	translate_error.clear();
}