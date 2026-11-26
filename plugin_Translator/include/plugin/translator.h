#ifndef PLUGIN_TRANSLATOR_H
#define PLUGIN_TRANSLATOR_H

#include <util/export.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef int32_t plugin_translator_res;

	enum plugin_translator_res_ {
		plugin_translator_res_ok = 1,
		plugin_translator_res_error = 0,
	};

	PLUGIN_API plugin_translator_res PLUGIN_CALL
		plugin_translator_register(const char* srcLanguage, const char* tarLanguage, const char* modelDir, char** error);

	PLUGIN_API plugin_translator_res PLUGIN_CALL
		plugin_translator_unload(const char* srcLanguage, const char* tarLanguage, char** error);

	PLUGIN_API plugin_translator_res PLUGIN_CALL
		plugin_translator_translate(const char* text, const char* srcLanguage, const char* tarLanguage, char** out, char** error);

	PLUGIN_API void PLUGIN_CALL
		plugin_translator_close();

#ifdef __cplusplus
}
#endif

#endif// PLUGIN_TRANSLATOR_H