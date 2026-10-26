#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "core/FileSystem.h"
#include "core/Localization.h"
#include "core/Log.h"

int main(int argc, char** argv) {
    it::fs::setDataRoot(IRONTIDE_DATA_DIR);
    it::setLogLevel(it::LogLevel::Warn);
    it::loc::load("en");
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    return ctx.run();
}
