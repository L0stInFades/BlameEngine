#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Next {
class Window;
class Renderer;
class Input;
class World;
}

namespace Next::Streaming {
class StreamingManager;
}

namespace Song {

struct GameOptions {
    bool runSelfTests = false;
    bool allowPlaceholderCells = false;
    uint32_t smokeFrames = 0;
    double smokeSeconds = 0.0;
    uint64_t rendererResourcePoolBudgetMB = 0;
};

class Game {
public:
    explicit Game(const GameOptions& options = {});
    ~Game();

    bool Initialize();
    void Shutdown();

    void Run();
    void Tick(float deltaTime);

private:
    bool InitializeEngine();
    void ShutdownEngine();

    void HandleInput(float deltaTime);
    void UpdateGame(float deltaTime);
    void Render(float deltaTime);
    void QueueDemoTextureUpload();
    void PollRendererTextureUpload();
    void RunJobSystemSelfTest();
    void RunAssetSystemTest();
    void RunECSSelfTest();

    GameOptions options_;
    bool running_;
    bool initialized_ = false;
    Next::Window* window_ = nullptr;
    Next::Renderer* renderer_ = nullptr;
    Next::Input* input_ = nullptr;
    std::unique_ptr<Next::World> world_;

    // CP7: World Streaming integration (kept in Game until Runtime owns it).
    std::unique_ptr<Next::Streaming::StreamingManager> streaming_;

    // Minimal camera state for driving streaming in the demo.
    float camX_ = 0.0f;
    float camY_ = 0.0f;
    float camZ_ = 0.0f;
    float lastCamX_ = 0.0f;
    float lastCamY_ = 0.0f;
    float lastCamZ_ = 0.0f;
    uint64_t demoTextureUploadId_ = 0;
    uint64_t demoRendererTextureId_ = 0;
    uint64_t demoNormalTextureUploadId_ = 0;
    uint64_t demoNormalRendererTextureId_ = 0;
    uint64_t demoMetallicRoughnessTextureUploadId_ = 0;
    uint64_t demoMetallicRoughnessRendererTextureId_ = 0;
    uint64_t demoEmissiveTextureUploadId_ = 0;
    uint64_t demoEmissiveRendererTextureId_ = 0;
    uint64_t demoOcclusionTextureUploadId_ = 0;
    uint64_t demoOcclusionRendererTextureId_ = 0;
    uint64_t demoRendererMaterialId_ = 0;
    bool demoTextureUploadFinalLogged_ = false;
    bool demoNormalTextureUploadFinalLogged_ = false;
    bool demoMetallicRoughnessTextureUploadFinalLogged_ = false;
    bool demoEmissiveTextureUploadFinalLogged_ = false;
    bool demoOcclusionTextureUploadFinalLogged_ = false;
    size_t lastFrameDebugCellCount_ = 0;
    size_t lastFrameRenderedDebugCellCount_ = 0;
    size_t lastFrameDebugCellOverflowCount_ = 0;
    size_t lastFramePlaceholderDebugCellCount_ = 0;
    size_t lastFrameRenderedPlaceholderDebugCellCount_ = 0;
    bool frameDebugOverflowLogged_ = false;
};

} // namespace Song
