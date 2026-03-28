/**
 * tjsdump — TJS2 bytecode disassembler
 *
 * Reads compiled TJS2 bytecode files (.tjs) and outputs
 * human-readable disassembly.
 *
 * Usage: tjsdump <file.tjs> [file2.tjs ...]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "tjs.h"
#include "tjsByteCodeLoader.h"
#include "tjsScriptBlock.h"

using namespace TJS;

// Stubs for symbols referenced by tjs2 library but not needed for disassembly
ttstr TVPGetMessageByLocale(const std::string &msg) { return ttstr(msg.c_str()); }

// Custom console output that prints to stdout
class StdoutConsoleOutput : public iTJSConsoleOutput {
public:
    void ExceptionPrint(const tjs_char *msg) override { Print(msg); }
    void Print(const tjs_char *msg) override {
        ttstr s(msg);
        printf("%s\n", s.AsStdString().c_str());
    }
};

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.tjs> [file2.tjs ...]\n", argv[0]);
        fprintf(stderr, "Disassembles compiled TJS2 bytecode files.\n");
        return 1;
    }

    // Create a minimal TJS2 engine instance with stdout output
    StdoutConsoleOutput consoleOutput;
    tTJS *engine = new tTJS();
    engine->SetConsoleOutput(&consoleOutput);

    for (int i = 1; i < argc; i++) {
        const char *filename = argv[i];

        // Read the file
        std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
        if (!ifs) {
            fprintf(stderr, "Error: cannot open '%s'\n", filename);
            continue;
        }

        auto size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        std::vector<tjs_uint8> buffer(size);
        ifs.read(reinterpret_cast<char *>(buffer.data()), size);
        ifs.close();

        // Check if it's TJS2 bytecode
        if (size < 8 || !tTJSByteCodeLoader::IsTJS2ByteCode(buffer.data())) {
            fprintf(stderr, "Error: '%s' is not a TJS2 bytecode file\n",
                    filename);
            continue;
        }

        // Extract just the filename for display
        std::string name(filename);
        auto slash = name.find_last_of("/\\");
        if (slash != std::string::npos)
            name = name.substr(slash + 1);

        // Convert filename to wide string
        ttstr wname(name.c_str());

        // Load the bytecode
        tTJSByteCodeLoader loader;
        tTJSScriptBlock *block =
            loader.ReadByteCode(engine, wname.c_str(), buffer.data(),
                                static_cast<tjs_uint>(size));

        if (!block) {
            fprintf(stderr, "Error: failed to load bytecode from '%s'\n",
                    filename);
            continue;
        }

        printf("===== %s (%u contexts) =====\n", filename,
               block->GetContextCount());

        // Dump disassembly to stdout via console output
        block->Dump();

        block->Release();
    }

    engine->Shutdown();
    engine->Release();

    return 0;
}
