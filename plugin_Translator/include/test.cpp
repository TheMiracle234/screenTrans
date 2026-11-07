#include "OfflineTranslator.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: example01_basic <model-directory>\n";
        return 2;
    }

    offline_translator::Translator translator;
    std::string error;
    if (!translator.registerModel("zh", "en", std::filesystem::path(argv[1]), &error)) {
        std::cerr << "Model registration failed: " << error << '\n';
        return 1;
    }

    translator.setDefaultLanguages("zh", "en");

    auto result = translator.translate("你好，世界！");
    if (!result.ok) {
        std::cerr << "Translation failed: " << result.error << "\nline: " << __LINE__ << "\n";
        return 1;
    }
    std::cout << "translate result: " << result.text << "\n";

    system("pause");
}