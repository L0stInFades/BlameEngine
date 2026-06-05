#include "next/platform/window.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#endif

namespace Next {
namespace testing {

using ::testing::Test;

#ifdef _WIN32
namespace {

bool RequestedClientSizeFitsMonitor(Window& window, int width, int height) {
    auto* hwnd = static_cast<HWND>(window.GetNativeHandle());
    if (!hwnd) {
        return false;
    }

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
        return true;
    }

    RECT outer = {0, 0, width, height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrA(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrA(hwnd, GWL_EXSTYLE));

    bool adjusted = false;
    if (HMODULE user32 = GetModuleHandleA("user32.dll")) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto* getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        auto* adjustForDpi =
            reinterpret_cast<AdjustWindowRectExForDpiFn>(GetProcAddress(user32, "AdjustWindowRectExForDpi"));
        if (getDpiForWindow && adjustForDpi) {
            adjusted = adjustForDpi(&outer, style, FALSE, exStyle, getDpiForWindow(hwnd)) != FALSE;
        }
    }

    if (!adjusted) {
        AdjustWindowRectEx(&outer, style, FALSE, exStyle);
    }

    const int requestedOuterWidth = outer.right - outer.left;
    const int requestedOuterHeight = outer.bottom - outer.top;
    const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    return requestedOuterWidth <= workWidth && requestedOuterHeight <= workHeight;
}

} // namespace
#endif

class WindowTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
    }

    void TearDown() override {
        Logger::Shutdown();
    }
};

// Test window creation
TEST_F(WindowTest, CreateWindow) {
    Window* window = CreateWindow();

    EXPECT_NE(window, nullptr);

    window->Shutdown();
    delete window;
}

// Test window initialization with default descriptor
TEST_F(WindowTest, InitializeWithDefaults) {
    Window* window = CreateWindow();

    WindowDesc desc;
    bool success = window->Initialize(desc);

    EXPECT_TRUE(success);
    EXPECT_FALSE(window->ShouldClose());

    window->Shutdown();
    delete window;
}

// Test window initialization with custom descriptor
TEST_F(WindowTest, InitializeWithCustomDesc) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.title = "Test Window";
    desc.width = 800;
    desc.height = 600;
    desc.fullscreen = false;
    desc.resizable = true;

    bool success = window->Initialize(desc);

    EXPECT_TRUE(success);
    EXPECT_EQ(window->GetWidth(), 800);
    EXPECT_EQ(window->GetHeight(), 600);

    window->Shutdown();
    delete window;
}

// Test window dimensions
TEST_F(WindowTest, WindowDimensions) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.width = 1024;
    desc.height = 768;

    window->Initialize(desc);

    EXPECT_EQ(window->GetWidth(), 1024);
    EXPECT_EQ(window->GetHeight(), 768);

    window->Shutdown();
    delete window;
}

// Test window resize
TEST_F(WindowTest, WindowResize) {
    Window* window = CreateWindow();

    WindowDesc desc;
    window->Initialize(desc);

    window->Resize(640, 480);

    EXPECT_EQ(window->GetWidth(), 640);
    EXPECT_EQ(window->GetHeight(), 480);

    window->Shutdown();
    delete window;
}

// Test window title
TEST_F(WindowTest, WindowTitle) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.title = "Original Title";
    window->Initialize(desc);

    window->SetTitle("New Title");

    // Title change should not crash
    SUCCEED();

    window->Shutdown();
    delete window;
}

// Test window poll events
TEST_F(WindowTest, PollEvents) {
    Window* window = CreateWindow();

    WindowDesc desc;
    window->Initialize(desc);

    // Poll events should not crash
    window->PollEvents();

    EXPECT_FALSE(window->ShouldClose());

    window->Shutdown();
    delete window;
}

// Test window swap buffers
TEST_F(WindowTest, SwapBuffers) {
    Window* window = CreateWindow();

    WindowDesc desc;
    window->Initialize(desc);

    // Swap buffers should not crash
    window->SwapBuffers();

    window->Shutdown();
    delete window;
}

// Test window native handle
TEST_F(WindowTest, NativeHandle) {
    Window* window = CreateWindow();

    WindowDesc desc;
    window->Initialize(desc);

    void* handle = window->GetNativeHandle();

    // Native handle should not be null on Windows
    #ifdef _WIN32
    EXPECT_NE(handle, nullptr);
    #endif

    window->Shutdown();
    delete window;
}

// Test window resize callback
TEST_F(WindowTest, ResizeCallback) {
    Window* window = CreateWindow();

    WindowDesc desc;
    window->Initialize(desc);

    bool callbackCalled = false;
    int callbackWidth = 0;
    int callbackHeight = 0;

    window->SetResizeCallback([&](int width, int height) {
        callbackCalled = true;
        callbackWidth = width;
        callbackHeight = height;
    });

    // Trigger resize
    window->Resize(800, 600);

    // Note: Callback might not be called immediately in all implementations
    // This test verifies the callback can be set without crashing
    SUCCEED();

    window->Shutdown();
    delete window;
}

// Test multiple initialization/shutdown cycles
TEST_F(WindowTest, MultipleInitShutdown) {
    Window* window = CreateWindow();

    for (int i = 0; i < 3; ++i) {
        WindowDesc desc;
        std::string title = "Cycle " + std::to_string(i);
        desc.title = title.c_str();

        EXPECT_TRUE(window->Initialize(desc));
        EXPECT_FALSE(window->ShouldClose());

        window->Shutdown();
    }

    delete window;
}

// Test fullscreen flag
TEST_F(WindowTest, FullscreenFlag) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.fullscreen = true;

    bool success = window->Initialize(desc);

    // Fullscreen might not be supported in all environments
    // We just verify it doesn't crash
    if (success) {
        window->Shutdown();
    }

    delete window;
}

// Test resizable flag
TEST_F(WindowTest, ResizableFlag) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.resizable = false;

    bool success = window->Initialize(desc);

    EXPECT_TRUE(success);

    window->Shutdown();
    delete window;
}

// Test window desc defaults
TEST_F(WindowTest, WindowDescDefaults) {
    WindowDesc desc;

    EXPECT_STREQ(desc.title, "NEXT Engine");
    EXPECT_EQ(desc.width, 1280);
    EXPECT_EQ(desc.height, 720);
    EXPECT_FALSE(desc.fullscreen);
    EXPECT_TRUE(desc.resizable);
}

// Test small window size
TEST_F(WindowTest, SmallWindowSize) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.width = 320;
    desc.height = 240;

    bool success = window->Initialize(desc);

    EXPECT_TRUE(success);
    EXPECT_EQ(window->GetWidth(), 320);
    EXPECT_EQ(window->GetHeight(), 240);

    window->Shutdown();
    delete window;
}

// Test large window size
TEST_F(WindowTest, LargeWindowSize) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.width = 1920;
    desc.height = 1080;

    bool success = window->Initialize(desc);

    EXPECT_TRUE(success);
#ifdef _WIN32
    if (RequestedClientSizeFitsMonitor(*window, desc.width, desc.height)) {
        EXPECT_EQ(window->GetWidth(), 1920);
        EXPECT_EQ(window->GetHeight(), 1080);
    } else {
        EXPECT_GT(window->GetWidth(), 0);
        EXPECT_GT(window->GetHeight(), 0);
        EXPECT_LE(window->GetWidth(), 1920);
        EXPECT_LE(window->GetHeight(), 1080);
    }
#else
    EXPECT_EQ(window->GetWidth(), 1920);
    EXPECT_EQ(window->GetHeight(), 1080);
#endif

    window->Shutdown();
    delete window;
}

// Test window with special characters in title
TEST_F(WindowTest, SpecialCharsInTitle) {
    Window* window = CreateWindow();

    WindowDesc desc;
    desc.title = "测试窗口 🎮";

    bool success = window->Initialize(desc);

    EXPECT_TRUE(success);

    window->Shutdown();
    delete window;
}

} // namespace testing
} // namespace Next

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
