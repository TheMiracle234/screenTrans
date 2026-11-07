#ifndef PLUGIN_TRANSLATOR_H
#define PLUGIN_TRANSLATOR_H

#ifdef _WIN32
#define PLUGIN_TRANSLATOR_CALL __cdecl
#if defined(PLUGIN_TRANSLATOR_EXPORTS)
#define PLUGIN_TRANSLATOR_API __declspec(dllexport)
#else
#define PLUGIN_TRANSLATOR_API
#endif
#else
#define PLUGIN_TRANSLATOR_CALL
#define PLUGIN_TRANSLATOR_API __attribute__((visibility("default")))
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef int32_t plugin_translator_res;

	enum plugin_translator_res_ {
		plugin_translator_res_ok = 1,
		plugin_translator_res_error = 0,
	};

	PLUGIN_TRANSLATOR_API plugin_translator_res PLUGIN_TRANSLATOR_CALL
		plugin_translator_register(const char* srcLanguage, const char* tarLanguage, const char* modelDir, char** error);

	PLUGIN_TRANSLATOR_API plugin_translator_res PLUGIN_TRANSLATOR_CALL
		plugin_translator_unload(const char* srcLanguage, const char* tarLanguage, char** error);

	PLUGIN_TRANSLATOR_API plugin_translator_res PLUGIN_TRANSLATOR_CALL
		plugin_translator_translate(const char* text, const char* srcLanguage, const char* tarLanguage, char** out, char** error);

	PLUGIN_TRANSLATOR_API void PLUGIN_TRANSLATOR_CALL
		plugin_translator_close();

#ifdef __cplusplus
}
#endif

#endif// PLUGIN_TRANSLATOR_H