#ifdef IMGUI_SMASH_ENABLE_NGPU_BRIDGE

#include "imgui_nvn.h"

#include "helpers/memoryHelper.h"
#include "imgui.h"
#include "logger/Logger.hpp"
#include "nvn_CppMethods.h"

extern bool IS_DRAWING;
extern bool hasInitImGui;
extern nvn::Device *nvnDevice;
extern nvn::Queue *nvnQueue;
extern nvn::CommandBuffer *nvnCmdBuf;
extern nvn::DeviceGetProcAddressFunc tempGetProcAddressFuncPtr;

nvn::Texture *nvnActiveWindowTexture = nullptr;

static constexpr size_t IMGUI_BRIDGE_COMMAND_BUFFER_BYTES = 0x40000;
static constexpr size_t IMGUI_BRIDGE_CONTROL_BUFFER_BYTES = 0x20000;

static nvn::Window *sBridgeWindow = nullptr;
static nvn::CommandBuffer sBridgeCmdBuf{};
static nvn::MemoryPool sBridgeCmdMemoryPool{};
static void *sBridgeCmdMemory = nullptr;
static void *sBridgeControlMemory = nullptr;
static size_t sBridgeCmdMemorySize = 0;
static size_t sBridgeControlMemorySize = 0;
static bool sBridgeCmdBufReady = false;
static bool sBridgeMemoryReady = false;

static bool ensure_bridge_command_buffer() {
  if (sBridgeCmdBufReady) {
    return true;
  }
  if (nvnDevice == nullptr || tempGetProcAddressFuncPtr == nullptr) {
    return false;
  }
  nvn::nvnLoadCPPProcs(nvnDevice, tempGetProcAddressFuncPtr);

  int commandAlignment = 0x1000;
  int controlAlignment = 0x1000;
  nvnDevice->GetInteger(nvn::DeviceInfo::COMMAND_BUFFER_COMMAND_ALIGNMENT, &commandAlignment);
  nvnDevice->GetInteger(nvn::DeviceInfo::COMMAND_BUFFER_CONTROL_ALIGNMENT, &controlAlignment);

  size_t commandAlign = commandAlignment > 0 ? static_cast<size_t>(commandAlignment) : 0x1000;
  size_t controlAlign = controlAlignment > 0 ? static_cast<size_t>(controlAlignment) : 0x1000;
  if (commandAlign < 0x1000) {
    commandAlign = 0x1000;
  }
  if (controlAlign < 0x1000) {
    controlAlign = 0x1000;
  }

  sBridgeCmdMemorySize = ALIGN_UP(IMGUI_BRIDGE_COMMAND_BUFFER_BYTES, commandAlign);
  sBridgeControlMemorySize = ALIGN_UP(IMGUI_BRIDGE_CONTROL_BUFFER_BYTES, controlAlign);

  if (!sBridgeMemoryReady) {
    Mem::Init();
    sBridgeMemoryReady = true;
  }
  if (sBridgeCmdMemory == nullptr) {
    sBridgeCmdMemory = Mem::AllocateAlign(commandAlign, sBridgeCmdMemorySize);
  }
  if (sBridgeControlMemory == nullptr) {
    sBridgeControlMemory = Mem::AllocateAlign(controlAlign, sBridgeControlMemorySize);
  }
  if (sBridgeCmdMemory == nullptr || sBridgeControlMemory == nullptr) {
    Logger::log("Failed to allocate bridge command buffer memory.\n");
    return false;
  }

  nvn::MemoryPoolBuilder bridgePoolBuilder{};
  bridgePoolBuilder.SetDefaults()
      .SetDevice(nvnDevice)
      .SetFlags(nvn::MemoryPoolFlags::CPU_UNCACHED | nvn::MemoryPoolFlags::GPU_UNCACHED)
      .SetStorage(sBridgeCmdMemory, sBridgeCmdMemorySize);

  if (!sBridgeCmdMemoryPool.Initialize(&bridgePoolBuilder)) {
    Logger::log("Failed to initialize bridge command buffer memory pool.\n");
    return false;
  }
  if (!sBridgeCmdBuf.Initialize(nvnDevice)) {
    Logger::log("Failed to initialize bridge command buffer.\n");
    return false;
  }

  // Bridge mode keeps a dedicated command buffer instead of reusing the
  // caller's recording path. That keeps the integration small, but can cost
  // some extra setup/submission work versus the original path.
  sBridgeCmdBuf.AddCommandMemory(&sBridgeCmdMemoryPool, 0, sBridgeCmdMemorySize);
  sBridgeCmdBuf.AddControlMemory(sBridgeControlMemory, sBridgeControlMemorySize);
  sBridgeCmdBuf.SetDebugLabel("imgui_smash_bridge_cmdbuf");

  nvnCmdBuf = &sBridgeCmdBuf;
  sBridgeCmdBufReady = true;
  return true;
}

static void rebind_bridge_command_buffer_memory() {
  if (!sBridgeCmdBufReady) {
    return;
  }

  sBridgeCmdBuf.AddCommandMemory(&sBridgeCmdMemoryPool, 0, sBridgeCmdMemorySize);
  sBridgeCmdBuf.AddControlMemory(sBridgeControlMemory, sBridgeControlMemorySize);
}

static void refresh_display_size_from_window() {
  if (!hasInitImGui || sBridgeWindow == nullptr) {
    return;
  }

  struct WindowCropRect {
    int x;
    int y;
    int width;
    int height;
  } crop{};

  sBridgeWindow->GetCrop(reinterpret_cast<nvn::Rectangle *>(&crop));
  if (crop.width <= 0 || crop.height <= 0) {
    return;
  }

  // Bridge mode updates ImGui's display size from the current window crop so
  // external presenters can resize dynamically. The original path keeps the
  // built-in viewport defaults.
  ImGuiIO &io = ImGui::GetIO();
  const float width = static_cast<float>(crop.width);
  const float height = static_cast<float>(crop.height);
  if (io.DisplaySize.x != width || io.DisplaySize.y != height) {
    io.DisplaySize = ImVec2(width, height);
  }
}

static void update_bridge_state(
    nvn::Device *device,
    nvn::Queue *queue,
    nvn::Window *window,
    void *procAddress) {
  if (device != nullptr) {
    nvnDevice = device;
  }
  if (queue != nullptr) {
    nvnQueue = queue;
  }
  if (window != nullptr) {
    sBridgeWindow = window;
  }
  if (procAddress != nullptr) {
    tempGetProcAddressFuncPtr = reinterpret_cast<nvn::DeviceGetProcAddressFunc>(procAddress);
  }
}

static bool ensure_bridge_ready() {
  if (nvnDevice == nullptr || nvnQueue == nullptr || tempGetProcAddressFuncPtr == nullptr) {
    return false;
  }
  if (!ensure_bridge_command_buffer()) {
    return false;
  }
  if (!hasInitImGui) {
    hasInitImGui = nvnImGui::InitImGui();
  }
  return hasInitImGui;
}

extern "C" bool imgui_smash_try_initialize_with_ngpu_bridge(
    nvn::Device *device,
    nvn::Queue *queue,
    nvn::Window *window,
    void *procAddress) {
  update_bridge_state(device, queue, window, procAddress);
  if (!ensure_bridge_ready()) {
    return false;
  }

  refresh_display_size_from_window();
  return true;
}

extern "C" uint64_t imgui_smash_render_from_ngpu_present(
    nvn::Queue *queue,
    nvn::Window *window,
    nvn::Texture *activeTexture) {
  update_bridge_state(nullptr, queue, window, nullptr);
  nvnActiveWindowTexture = activeTexture;

  if (nvnActiveWindowTexture == nullptr || IS_DRAWING) {
    return 0;
  }
  if (!ensure_bridge_ready()) {
    return 0;
  }

  rebind_bridge_command_buffer_memory();
  refresh_display_size_from_window();
  return nvnImGui::procDraw();
}

#endif
