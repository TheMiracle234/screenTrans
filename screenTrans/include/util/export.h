#pragma once
#ifndef FM_EXPORT_H
#define FM_EXPORT_H

#ifdef _WIN32
#	define PLUGIN_CALL __cdecl
#	ifdef PLUGIN_EXPORTS
#		define PLUGIN_API __declspec(dllexport)
#	else
#		define PLUGIN_API
#	endif
#else
#	define PLUGIN_CALL
#	define PLUGIN_API __attribute__((visibility("default")))
#endif

#endif // FM_EXPORT_H