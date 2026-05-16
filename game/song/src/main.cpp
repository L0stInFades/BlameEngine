#include "song/game.h"
#include "next/foundation/logger.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>

int main(int argc, char* argv[]) {
    NEXT_LOG_INFO("========================================");
    NEXT_LOG_INFO("NEXT Engine - Song Dynasty Demo (CP0)");
    NEXT_LOG_INFO("========================================");

    Song::GameOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? argv[i] : "";
        if (arg == "--run-self-tests") {
            options.runSelfTests = true;
        } else if (arg == "--allow-placeholder-cells") {
            options.allowPlaceholderCells = true;
        } else if (arg == "--smoke-frames" && i + 1 < argc) {
            options.smokeFrames = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--smoke-seconds" && i + 1 < argc) {
            options.smokeSeconds = std::atof(argv[++i]);
        } else if (arg == "--renderer-pool-budget-mb" && i + 1 < argc) {
            options.rendererResourcePoolBudgetMB = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--help" || arg == "-h") {
            std::fprintf(stdout,
                         "song_demo\n"
                         "  --run-self-tests           Run startup self-tests before entering the loop\n"
                         "  --allow-placeholder-cells  Allow world streaming to synthesize placeholder cells\n"
                         "  --smoke-frames <n>         Run for n frames then exit\n"
                         "  --smoke-seconds <n>        Run for n seconds then exit\n"
                         "  --renderer-pool-budget-mb <n>  Apply a renderer resource pool budget per memory bucket\n");
            return 0;
        }
    }

    Song::Game game(options);

    if (!game.Initialize()) {
        NEXT_LOG_FATAL("Failed to initialize game");
        return 1;
    }

    game.Run();

    game.Shutdown();

    NEXT_LOG_INFO("========================================");
    NEXT_LOG_INFO("Game exited normally");
    NEXT_LOG_INFO("========================================");

    return 0;
}
