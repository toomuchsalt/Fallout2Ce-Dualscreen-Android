// Native executable so Dolphin offers Run rather than opening a Python file.
// Python and KDialog are already part of Armada's KDE desktop image.
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

static int error()
{
    const char* message = "Fallout 2 setup could not start. Extract the complete download and run Install Fallout 2 in Armada's Desktop Mode.";
    execlp("kdialog", "kdialog", "--title", "Fallout 2 (Armada)", "--error", message, nullptr);
    execlp("zenity", "zenity", "--error", "--text", message, nullptr);
    return 1;
}

int main(int argc, char** argv)
{
    std::error_code ec;
    auto location = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return error();
    std::string script = (location.parent_path() / "share/fallout2-armada/armada_setup.py").string();
    if (!std::filesystem::is_regular_file(script, ec)) return error();
    std::vector<char*> arguments { const_cast<char*>("python3"), script.data() };
    for (int i = 1; i < argc; ++i) arguments.push_back(argv[i]);
    arguments.push_back(nullptr);
    execvp("python3", arguments.data());
    return error();
}
