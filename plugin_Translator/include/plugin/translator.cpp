#define PLUGIN_TRANSLATOR_EXPORTS
#include <plugin/translator.h>
#include <OfflineTranslator.hpp>

#include <cassert>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using offline_translator::Translator;
using offline_translator::Result;

static Translator translator{};
static std::string register_error{};
static std::string unload_error{};
static std::string translate_out{};
static std::string translate_error{};

plugin_translator_res PLUGIN_TRANSLATOR_CALL plugin_translator_register(const char* srcLanguage, const char* tarLanguage, const char* modelDir, char** error) {
	bool ok = translator.registerModel(srcLanguage, tarLanguage, fs::path{ modelDir }, &register_error);
	if (!ok) [[unlikely]] {
		*error = register_error.data();
		return plugin_translator_res_error;
	}
	translator.setDefaultLanguages(srcLanguage, tarLanguage);
	return plugin_translator_res_ok;
}

plugin_translator_res PLUGIN_TRANSLATOR_CALL
plugin_translator_unload(const char* srcLanguage, const char* tarLanguage, char** error) {
	bool ok = translator.unloadModel(srcLanguage, tarLanguage, &unload_error);
	if (!ok) {
		*error = unload_error.data();
		return plugin_translator_res_error;
	}
	return plugin_translator_res_ok;
}

plugin_translator_res PLUGIN_TRANSLATOR_CALL plugin_translator_translate(const char* text, const char* srcLanguage, const char* tarLanguage, char** out, char** error) {
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

void PLUGIN_TRANSLATOR_CALL plugin_translator_close() {
	translator = {};
	register_error.clear();
	translate_out.clear();
	translate_error.clear();
}