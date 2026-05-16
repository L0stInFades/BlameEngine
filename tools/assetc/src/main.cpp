#include "next/foundation/logger.h"
#include "asset_compiler.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    Next::Logger::Initialize();
    
    std::cout << "NEXT Asset Compiler (CP3)" << std::endl;
    std::cout << "==========================" << std::endl;
    
    if (argc < 2) {
        std::cout << "Usage:" << std::endl;
        std::cout << "  next_assetc test <output_dir>  - Generate test assets for CP3" << std::endl;
        std::cout << "  next_assetc compile <input> <output> - Compile single asset" << std::endl;
        std::cout << "  next_assetc cell <input.bin> <output.ncell> [none|lz4|zstd] - Compile streaming cell" << std::endl;
        std::cout << "  next_assetc import <input.obj> <output.npkg> - Compile a model into a package (mesh only for now)" << std::endl;
        std::cout << "  next_assetc package <name> <output> <assets...> - Create package" << std::endl;
        return 1;
    }
    
    Next::AssetCompiler compiler;
    std::string command = argv[1];
    
    if (command == "test") {
        if (argc < 3) {
            std::cout << "Error: Output directory required for test command" << std::endl;
            return 1;
        }
        
        std::string outputDir = argv[2];
        NEXT_LOG_INFO("Generating test assets in: %s", outputDir.c_str());
        
        if (!compiler.GenerateTestAssets(outputDir)) {
            NEXT_LOG_ERROR("Failed to generate test assets");
            return 1;
        }
        
        NEXT_LOG_INFO("Test assets generated successfully");
        std::cout << "Test assets generated in: " << outputDir << std::endl;
        
    } else if (command == "compile") {
        if (argc < 4) {
            std::cout << "Error: Input and output paths required for compile command" << std::endl;
            return 1;
        }
        
        std::string inputPath = argv[2];
        std::string outputPath = argv[3];
        
        // Determine asset type from extension
        std::filesystem::path path(inputPath);
        std::string ext = path.extension().string();
        
        bool success = false;
        if (ext == ".obj" || ext == ".fbx") {
            success = compiler.CompileMesh(inputPath, outputPath);
        } else if (ext == ".png" || ext == ".jpg" || ext == ".tga") {
            success = compiler.CompileTexture(inputPath, outputPath);
        } else if (ext == ".mat" || ext == ".material") {
            success = compiler.CompileMaterial(inputPath, outputPath);
        } else {
            NEXT_LOG_ERROR("Unsupported file extension: %s", ext.c_str());
            return 1;
        }
        
        if (!success) {
            NEXT_LOG_ERROR("Failed to compile asset: %s", inputPath.c_str());
            return 1;
        }
        
        NEXT_LOG_INFO("Asset compiled successfully: %s -> %s", inputPath.c_str(), outputPath.c_str());

    } else if (command == "cell") {
        if (argc < 4) {
            std::cout << "Error: Input payload and output .ncell paths required for cell command" << std::endl;
            return 1;
        }

        const std::string inputPath = argv[2];
        const std::string outputPath = argv[3];
        const std::string compression = argc >= 5 ? argv[4] : "none";

        if (!compiler.CompileCell(inputPath, outputPath, compression)) {
            NEXT_LOG_ERROR("Failed to compile streaming cell: %s", inputPath.c_str());
            return 1;
        }

        NEXT_LOG_INFO("Streaming cell compiled successfully: %s -> %s",
                      inputPath.c_str(), outputPath.c_str());

    } else if (command == "import") {
        if (argc < 4) {
            std::cout << "Error: Input OBJ and output package paths required for import command" << std::endl;
            return 1;
        }

        const std::string inputPath = argv[2];
        const std::string packagePath = argv[3];

        std::filesystem::path in(inputPath);
        if (in.extension().string() != ".obj") {
            NEXT_LOG_ERROR("Import currently only supports .obj (got %s)", in.extension().string().c_str());
            return 1;
        }

        std::filesystem::path outPkg(packagePath);
        const std::string packageName = outPkg.stem().string().empty() ? in.stem().string() : outPkg.stem().string();

        std::filesystem::path intermediateDir = outPkg.parent_path() / (packageName + "_compiled");
        std::filesystem::create_directories(intermediateDir);

        std::filesystem::path meshOut = intermediateDir / (packageName + ".mesh");

        if (!compiler.CompileMesh(inputPath, meshOut.string())) {
            NEXT_LOG_ERROR("Import failed: mesh compilation failed");
            return 1;
        }

        std::vector<std::string> assets;
        assets.push_back(meshOut.string());

        if (!compiler.CreatePackage(packageName, assets, packagePath)) {
            NEXT_LOG_ERROR("Import failed: package creation failed");
            return 1;
        }

        NEXT_LOG_INFO("Import succeeded: %s -> %s", inputPath.c_str(), packagePath.c_str());
        
    } else if (command == "package") {
        if (argc < 5) {
            std::cout << "Error: Package name, output path, and asset files required" << std::endl;
            return 1;
        }
        
        std::string packageName = argv[2];
        std::string outputPath = argv[3];
        std::vector<std::string> assetFiles;
        
        for (int i = 4; i < argc; ++i) {
            assetFiles.push_back(argv[i]);
        }
        
        NEXT_LOG_INFO("Creating package: %s with %zu assets", packageName.c_str(), assetFiles.size());
        
        if (!compiler.CreatePackage(packageName, assetFiles, outputPath)) {
            NEXT_LOG_ERROR("Failed to create package");
            return 1;
        }
        
        NEXT_LOG_INFO("Package created successfully: %s", outputPath.c_str());
        
    } else {
        std::cout << "Error: Unknown command: " << command << std::endl;
        return 1;
    }
    
    Next::Logger::Shutdown();
    return 0;
}
