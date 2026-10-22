#include "experience.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    try {
        for(const auto& step:pm::ui::steps) if(!*step.title || !*step.body || !*step.action) throw std::runtime_error("empty guide step");
        const auto unknown=pm::ui::failure("SECRET_TOKEN_DEVICE_PATH");
        if(pm::ui::diagnostics(unknown).find(L"SECRET")!=std::wstring::npos) throw std::runtime_error("diagnostic leaks input");
        if(pm::ui::failure("DEVICE_UNAUTHORIZED").code!="DEVICE_UNAUTHORIZED") throw std::runtime_error("authorization state lost");
        std::cout<<"Guide and privacy-safe diagnostics passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
