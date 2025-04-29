# Vulkan-Game-Engine

A custom game-engine being developed in Vulkan C++

## 🛠️ Building with CMake (GUI & CLI)

### 🔷 Option 1: Using CMake GUI (Windows / Visual Studio)

1. Open **CMake GUI**.
2. **Set the source code path** to the root of this repository.
3. **Set the build directory** (e.g.:   `build/` ) — this is where the solution will be generated.
4. Click **"Configure"**:
   - Choose your preferred **generator**, e.g., *Visual Studio 17 2022*.
   - Optionally select the architecture, e.g., *x64*.
5. Click **"Generate"**.
6. Open the generated `.sln` file in the build folder with Visual Studio.

### 🔷 Option 2: Using Command Line (CMake + Visual Studio)

```bash
# From the root of the project
mkdir build
cd build

# Generate Visual Studio solution (adjust generator name as needed)
cmake .. -G "Visual Studio 17 2022" -A x64

# Optional: build the solution using CMake
cmake --build . --config Release
