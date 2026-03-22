#include "ew_gpu_compute.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#ifndef EW_VK_SHADER_OUT_DIR
#define EW_VK_SHADER_OUT_DIR "."
#endif

namespace {

constexpr int64_t kGeTopkSentinelScore = std::numeric_limits<int64_t>::min();
constexpr uint32_t kGeTopkSentinelIndex = 0xFFFFFFFFu;

struct EwVkBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t size = 0;
};

enum class EwVkShaderKind : uint32_t {
    GeCanonicalize = 0,
    SymbolTokenize,
    CarrierBundle,
    OverlapScores,
    TopkBlock,
    TopkMerge,
    Count
};

struct EwVkShaderDesc {
    const char* file_name = nullptr;
    uint32_t binding_count = 0;
    uint32_t push_constant_size = 0;
};

struct EwVkPipelineState {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool loaded = false;
};

static std::vector<uint32_t> ew_pack_bytes_to_words(const uint8_t* bytes, size_t len) {
    std::vector<uint32_t> words((len + 3u) / 4u, 0u);
    for (size_t i = 0; i < len; ++i) {
        const size_t word_i = i >> 2;
        const uint32_t shift = uint32_t((i & 3u) * 8u);
        words[word_i] |= uint32_t(bytes[i]) << shift;
    }
    return words;
}

static void ew_unpack_words_to_bytes(const uint32_t* words, size_t len, std::string& out) {
    out.clear();
    out.resize(len);
    for (size_t i = 0; i < len; ++i) {
        const size_t word_i = i >> 2;
        const uint32_t shift = uint32_t((i & 3u) * 8u);
        out[i] = char((words[word_i] >> shift) & 0xFFu);
    }
}

static std::vector<uint32_t> ew_promote_u16_to_u32(const uint16_t* src, uint32_t n) {
    std::vector<uint32_t> out(n);
    for (uint32_t i = 0; i < n; ++i) out[i] = uint32_t(src[i]);
    return out;
}

static std::vector<uint32_t> ew_read_spv_u32(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.good()) return {};
    const std::streamsize sz = in.tellg();
    if (sz <= 0 || (sz % 4) != 0) return {};
    std::vector<uint32_t> code(size_t(sz) / sizeof(uint32_t));
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(code.data()), sz)) return {};
    return code;
}

class EwVkContext {
public:
    static EwVkContext& instance();

    bool available();
    bool create_buffer(size_t size, EwVkBuffer& out);
    void destroy_buffer(EwVkBuffer& buffer);
    bool dispatch(EwVkShaderKind kind,
                  const std::vector<const EwVkBuffer*>& buffers,
                  const void* push_data,
                  uint32_t push_size,
                  uint32_t gx,
                  uint32_t gy = 1,
                  uint32_t gz = 1);

private:
    EwVkContext() = default;
    ~EwVkContext();
    bool ensure_initialized();
    bool load_pipeline(EwVkShaderKind kind);
    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) const;
    void shutdown();

    bool init_attempted_ = false;
    bool initialized_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice dev_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = UINT32_MAX;
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::array<EwVkPipelineState, size_t(EwVkShaderKind::Count)> pipelines_{};

    static constexpr std::array<EwVkShaderDesc, size_t(EwVkShaderKind::Count)> shader_descs_{{
        {"ge_canonicalize.comp.spv", 3u, 8u},
        {"symbol_tokenize.comp.spv", 6u, 8u},
        {"carrier_bundle.comp.spv", 7u, 8u},
        {"ge_overlap_scores.comp.spv", 4u, 12u},
        {"ge_topk_block.comp.spv", 3u, 12u},
        {"ge_topk_merge.comp.spv", 4u, 8u},
    }};
};

struct EwVkScopedBuffer {
    EwVkScopedBuffer() = default;
    explicit EwVkScopedBuffer(EwVkContext* in_ctx) : ctx(in_ctx) {}
    ~EwVkScopedBuffer() {
        if (ctx) ctx->destroy_buffer(buffer);
    }

    EwVkContext* ctx = nullptr;
    EwVkBuffer buffer{};
};

static bool ew_gpu_make_buffer(EwVkContext& ctx, size_t size, EwVkScopedBuffer& scoped) {
    scoped.ctx = &ctx;
    return ctx.create_buffer(size, scoped.buffer);
}

static inline bool ge_better_host(int64_t score_a, uint32_t idx_a, int64_t score_b, uint32_t idx_b) {
    if (score_a > score_b) return true;
    if (score_a < score_b) return false;
    return idx_a < idx_b;
}

EwVkContext& EwVkContext::instance() {
    static EwVkContext ctx;
    return ctx;
}

EwVkContext::~EwVkContext() {
    shutdown();
}

bool EwVkContext::available() {
    return ensure_initialized();
}

bool EwVkContext::create_buffer(size_t size, EwVkBuffer& out) {
    if (!ensure_initialized() || size == 0) return false;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev_, &bci, nullptr, &out.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev_, out.buffer, &req);

    const uint32_t memory_type = find_memory_type(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        destroy_buffer(out);
        return false;
    }

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(dev_, &mai, nullptr, &out.memory) != VK_SUCCESS) {
        destroy_buffer(out);
        return false;
    }
    if (vkBindBufferMemory(dev_, out.buffer, out.memory, 0) != VK_SUCCESS) {
        destroy_buffer(out);
        return false;
    }
    if (vkMapMemory(dev_, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped) != VK_SUCCESS) {
        destroy_buffer(out);
        return false;
    }
    out.size = size;
    return true;
}

void EwVkContext::destroy_buffer(EwVkBuffer& buffer) {
    if (buffer.mapped && buffer.memory) {
        vkUnmapMemory(dev_, buffer.memory);
    }
    if (buffer.buffer) vkDestroyBuffer(dev_, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(dev_, buffer.memory, nullptr);
    buffer = EwVkBuffer{};
}

bool EwVkContext::dispatch(EwVkShaderKind kind,
                           const std::vector<const EwVkBuffer*>& buffers,
                           const void* push_data,
                           uint32_t push_size,
                           uint32_t gx,
                           uint32_t gy,
                           uint32_t gz) {
    if (!ensure_initialized()) return false;
    if (!load_pipeline(kind)) return false;

    const auto& pipe = pipelines_[size_t(kind)];
    VkDescriptorPool pool = VK_NULL_HANDLE;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps.descriptorCount = uint32_t(buffers.size());

    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = buffers.empty() ? 0u : 1u;
    dpci.pPoolSizes = buffers.empty() ? nullptr : &ps;
    if (vkCreateDescriptorPool(dev_, &dpci, nullptr, &pool) != VK_SUCCESS) return false;

    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &pipe.set_layout;
    if (vkAllocateDescriptorSets(dev_, &dsai, &ds) != VK_SUCCESS) {
        vkDestroyDescriptorPool(dev_, pool, nullptr);
        return false;
    }

    std::vector<VkDescriptorBufferInfo> infos(buffers.size());
    std::vector<VkWriteDescriptorSet> writes(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        infos[i].buffer = buffers[i]->buffer;
        infos[i].offset = 0;
        infos[i].range = VK_WHOLE_SIZE;

        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = ds;
        writes[i].dstBinding = uint32_t(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    if (!writes.empty()) vkUpdateDescriptorSets(dev_, uint32_t(writes.size()), writes.data(), 0, nullptr);

    vkResetFences(dev_, 1, &fence_);
    vkResetCommandPool(dev_, cmd_pool_, 0);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cmd_, &bi) != VK_SUCCESS) {
        vkDestroyDescriptorPool(dev_, pool, nullptr);
        return false;
    }

    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.pipeline);
    vkCmdBindDescriptorSets(
        cmd_,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipe.pipeline_layout,
        0,
        1,
        &ds,
        0,
        nullptr);
    if (push_size > 0 && push_data) {
        vkCmdPushConstants(cmd_, pipe.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push_data);
    }
    vkCmdDispatch(cmd_, gx, gy, gz);

    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        cmd_,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0,
        1,
        &barrier,
        0,
        nullptr,
        0,
        nullptr);

    if (vkEndCommandBuffer(cmd_) != VK_SUCCESS) {
        vkDestroyDescriptorPool(dev_, pool, nullptr);
        return false;
    }

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    if (vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS) {
        vkDestroyDescriptorPool(dev_, pool, nullptr);
        return false;
    }
    if (vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        vkDestroyDescriptorPool(dev_, pool, nullptr);
        return false;
    }

    vkDestroyDescriptorPool(dev_, pool, nullptr);
    return true;
}

bool EwVkContext::ensure_initialized() {
    if (init_attempted_) return initialized_;
    init_attempted_ = true;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GenesisEngineCompute";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "GenesisEngine";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) return false;

    uint32_t dev_count = 0;
    if (vkEnumeratePhysicalDevices(instance_, &dev_count, nullptr) != VK_SUCCESS || dev_count == 0u) return false;
    std::vector<VkPhysicalDevice> devs(dev_count);
    if (vkEnumeratePhysicalDevices(instance_, &dev_count, devs.data()) != VK_SUCCESS) return false;

    phys_ = VK_NULL_HANDLE;
    queue_family_ = UINT32_MAX;
    for (VkPhysicalDevice dev : devs) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(dev, &features);
        if (!features.shaderInt64) continue;

        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());
        for (uint32_t qi = 0; qi < qcount; ++qi) {
            const VkQueueFlags flags = qprops[qi].queueFlags;
            if ((flags & VK_QUEUE_COMPUTE_BIT) || (flags & VK_QUEUE_GRAPHICS_BIT)) {
                if (phys_ == VK_NULL_HANDLE || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    phys_ = dev;
                    queue_family_ = qi;
                }
                break;
            }
        }
        if (phys_ != VK_NULL_HANDLE && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
    }

    if (phys_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) return false;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkPhysicalDeviceFeatures features{};
    features.shaderInt64 = VK_TRUE;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &features;
    if (vkCreateDevice(phys_, &dci, nullptr, &dev_) != VK_SUCCESS) return false;
    vkGetDeviceQueue(dev_, queue_family_, 0, &queue_);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = queue_family_;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(dev_, &cpci, nullptr, &cmd_pool_) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmd_pool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(dev_, &cbai, &cmd_) != VK_SUCCESS) return false;

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(dev_, &fci, nullptr, &fence_) != VK_SUCCESS) return false;

    initialized_ = true;
    return true;
}

bool EwVkContext::load_pipeline(EwVkShaderKind kind) {
    auto& pipe = pipelines_[size_t(kind)];
    if (pipe.loaded) return true;
    if (!ensure_initialized()) return false;

    const EwVkShaderDesc& desc = shader_descs_[size_t(kind)];

    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    for (uint32_t i = 0; i < desc.binding_count; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = desc.binding_count;
    dlci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(dev_, &dlci, nullptr, &pipe.set_layout) != VK_SUCCESS) return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = desc.push_constant_size;

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &pipe.set_layout;
    if (desc.push_constant_size > 0u) {
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;
    }
    if (vkCreatePipelineLayout(dev_, &plci, nullptr, &pipe.pipeline_layout) != VK_SUCCESS) return false;

    const std::filesystem::path spv_path = std::filesystem::path(EW_VK_SHADER_OUT_DIR) / desc.file_name;
    const std::vector<uint32_t> spv = ew_read_spv_u32(spv_path);
    if (spv.empty()) return false;

    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = spv.size() * sizeof(uint32_t);
    smci.pCode = spv.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev_, &smci, nullptr, &module) != VK_SUCCESS) return false;

    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.layout = pipe.pipeline_layout;
    cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = module;
    cpci.stage.pName = "main";

    const VkResult result = vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipe.pipeline);
    vkDestroyShaderModule(dev_, module, nullptr);
    if (result != VK_SUCCESS) return false;

    pipe.loaded = true;
    return true;
}

uint32_t EwVkContext::find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        const bool supported = (type_bits & (1u << i)) != 0u;
        const bool has_props = (mp.memoryTypes[i].propertyFlags & props) == props;
        if (supported && has_props) return i;
    }
    return UINT32_MAX;
}

void EwVkContext::shutdown() {
    if (dev_) {
        vkDeviceWaitIdle(dev_);
        for (auto& pipe : pipelines_) {
            if (pipe.pipeline) vkDestroyPipeline(dev_, pipe.pipeline, nullptr);
            if (pipe.pipeline_layout) vkDestroyPipelineLayout(dev_, pipe.pipeline_layout, nullptr);
            if (pipe.set_layout) vkDestroyDescriptorSetLayout(dev_, pipe.set_layout, nullptr);
            pipe = EwVkPipelineState{};
        }
        if (fence_) vkDestroyFence(dev_, fence_, nullptr);
        if (cmd_pool_) vkDestroyCommandPool(dev_, cmd_pool_, nullptr);
        vkDestroyDevice(dev_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    phys_ = VK_NULL_HANDLE;
    dev_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    queue_family_ = UINT32_MAX;
    cmd_pool_ = VK_NULL_HANDLE;
    cmd_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    initialized_ = false;
}

} // namespace

bool ew_gpu_compute_available() {
    return EwVkContext::instance().available();
}

bool ew_gpu_canonicalize_utf8_strict(
    const uint8_t* bytes_host,
    size_t len,
    std::string& out_canon_utf8,
    bool* out_invalid_utf8,
    uint32_t* out_paragraph_breaks
) {
    out_canon_utf8.clear();
    if (out_invalid_utf8) *out_invalid_utf8 = false;
    if (out_paragraph_breaks) *out_paragraph_breaks = 0u;
    if (!bytes_host || len == 0u) return true;

    EwVkContext& ctx = EwVkContext::instance();
    if (!ctx.available()) return false;

    const std::vector<uint32_t> in_words = ew_pack_bytes_to_words(bytes_host, len);
    const size_t out_words_count = (len + 3u) / 4u;

    EwVkScopedBuffer in_buf(&ctx);
    EwVkScopedBuffer out_buf(&ctx);
    EwVkScopedBuffer meta_buf(&ctx);
    if (!ew_gpu_make_buffer(ctx, in_words.size() * sizeof(uint32_t), in_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, out_words_count * sizeof(uint32_t), out_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, 4u * sizeof(uint32_t), meta_buf)) return false;

    std::memcpy(in_buf.buffer.mapped, in_words.data(), in_words.size() * sizeof(uint32_t));
    std::memset(out_buf.buffer.mapped, 0, out_words_count * sizeof(uint32_t));
    std::memset(meta_buf.buffer.mapped, 0, 4u * sizeof(uint32_t));

    struct Push {
        uint32_t in_len_u32;
        uint32_t out_cap_u32;
    } push{};
    push.in_len_u32 = (len > 0xFFFFFFFFu) ? 0xFFFFFFFFu : uint32_t(len);
    push.out_cap_u32 = push.in_len_u32;

    const std::vector<const EwVkBuffer*> buffers{
        &in_buf.buffer, &out_buf.buffer, &meta_buf.buffer
    };
    if (!ctx.dispatch(EwVkShaderKind::GeCanonicalize, buffers, &push, sizeof(push), 1u, 1u, 1u)) return false;

    const auto* meta = reinterpret_cast<const uint32_t*>(meta_buf.buffer.mapped);
    if (out_invalid_utf8) *out_invalid_utf8 = meta[1] != 0u;
    if (out_paragraph_breaks) *out_paragraph_breaks = meta[2];
    if (meta[1] != 0u) return false;

    ew_unpack_words_to_bytes(reinterpret_cast<const uint32_t*>(out_buf.buffer.mapped), meta[0], out_canon_utf8);
    return true;
}

bool ew_gpu_tokenize_symbols_batch(
    const uint8_t* bytes_concat,
    const uint32_t* offsets_u32,
    const uint32_t* lens_u32,
    const uint32_t* artifact_ids_u32,
    uint32_t count_u32,
    EwSymbolToken9* out_tokens,
    uint32_t* out_counts_u32_per_artifact,
    uint32_t max_tokens_per_artifact_u32
) {
    if (!bytes_concat || !offsets_u32 || !lens_u32 || !artifact_ids_u32 || !out_tokens || !out_counts_u32_per_artifact) return false;
    if (count_u32 == 0u || max_tokens_per_artifact_u32 == 0u) return false;

    EwVkContext& ctx = EwVkContext::instance();
    if (!ctx.available()) return false;

    uint32_t max_end = 0u;
    for (uint32_t i = 0; i < count_u32; ++i) {
        const uint32_t end = offsets_u32[i] + lens_u32[i];
        if (end > max_end) max_end = end;
    }

    const std::vector<uint32_t> byte_words = ew_pack_bytes_to_words(bytes_concat, max_end);
    const size_t token_bytes = size_t(count_u32) * size_t(max_tokens_per_artifact_u32) * sizeof(EwSymbolToken9);
    const size_t count_bytes = size_t(count_u32) * sizeof(uint32_t);

    EwVkScopedBuffer bytes_buf(&ctx);
    EwVkScopedBuffer offsets_buf(&ctx);
    EwVkScopedBuffer lens_buf(&ctx);
    EwVkScopedBuffer ids_buf(&ctx);
    EwVkScopedBuffer tokens_buf(&ctx);
    EwVkScopedBuffer counts_buf(&ctx);
    if (!ew_gpu_make_buffer(ctx, byte_words.size() * sizeof(uint32_t), bytes_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(count_u32) * sizeof(uint32_t), offsets_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(count_u32) * sizeof(uint32_t), lens_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(count_u32) * sizeof(uint32_t), ids_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, token_bytes, tokens_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, count_bytes, counts_buf)) return false;

    std::memcpy(bytes_buf.buffer.mapped, byte_words.data(), byte_words.size() * sizeof(uint32_t));
    std::memcpy(offsets_buf.buffer.mapped, offsets_u32, size_t(count_u32) * sizeof(uint32_t));
    std::memcpy(lens_buf.buffer.mapped, lens_u32, size_t(count_u32) * sizeof(uint32_t));
    std::memcpy(ids_buf.buffer.mapped, artifact_ids_u32, size_t(count_u32) * sizeof(uint32_t));
    std::memset(tokens_buf.buffer.mapped, 0, token_bytes);
    std::memset(counts_buf.buffer.mapped, 0, count_bytes);

    struct Push {
        uint32_t count_u32;
        uint32_t max_tokens_u32;
    } push{count_u32, max_tokens_per_artifact_u32};

    const std::vector<const EwVkBuffer*> buffers{
        &bytes_buf.buffer,
        &offsets_buf.buffer,
        &lens_buf.buffer,
        &ids_buf.buffer,
        &tokens_buf.buffer,
        &counts_buf.buffer
    };
    if (!ctx.dispatch(EwVkShaderKind::SymbolTokenize, buffers, &push, sizeof(push), count_u32, 1u, 1u)) return false;

    std::memcpy(out_tokens, tokens_buf.buffer.mapped, token_bytes);
    std::memcpy(out_counts_u32_per_artifact, counts_buf.buffer.mapped, count_bytes);
    return true;
}

bool ew_gpu_compute_carrier_triples(
    const int64_t* doppler_q_turns,
    const int64_t* m_q_turns,
    const int64_t* curvature_q_turns,
    const uint16_t* flux_grad_mean_q15,
    const uint16_t* harmonics_mean_q15,
    uint32_t anchors_n,
    const uint32_t* anchor_ids,
    uint32_t ids_n,
    EwCarrierTriple* out_triples
) {
    if (!doppler_q_turns || !m_q_turns || !curvature_q_turns || !flux_grad_mean_q15 || !harmonics_mean_q15 || !anchor_ids || !out_triples) return false;
    if (anchors_n == 0u || ids_n == 0u) return true;

    EwVkContext& ctx = EwVkContext::instance();
    if (!ctx.available()) return false;

    const std::vector<uint32_t> flux_u32 = ew_promote_u16_to_u32(flux_grad_mean_q15, anchors_n);
    const std::vector<uint32_t> harm_u32 = ew_promote_u16_to_u32(harmonics_mean_q15, anchors_n);

    EwVkScopedBuffer doppler_buf(&ctx);
    EwVkScopedBuffer mass_buf(&ctx);
    EwVkScopedBuffer curv_buf(&ctx);
    EwVkScopedBuffer flux_buf(&ctx);
    EwVkScopedBuffer harm_buf(&ctx);
    EwVkScopedBuffer ids_buf(&ctx);
    EwVkScopedBuffer out_buf(&ctx);

    if (!ew_gpu_make_buffer(ctx, size_t(anchors_n) * sizeof(int64_t), doppler_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(anchors_n) * sizeof(int64_t), mass_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(anchors_n) * sizeof(int64_t), curv_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(anchors_n) * sizeof(uint32_t), flux_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(anchors_n) * sizeof(uint32_t), harm_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(ids_n) * sizeof(uint32_t), ids_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, size_t(ids_n) * sizeof(EwCarrierTriple), out_buf)) return false;

    std::memcpy(doppler_buf.buffer.mapped, doppler_q_turns, size_t(anchors_n) * sizeof(int64_t));
    std::memcpy(mass_buf.buffer.mapped, m_q_turns, size_t(anchors_n) * sizeof(int64_t));
    std::memcpy(curv_buf.buffer.mapped, curvature_q_turns, size_t(anchors_n) * sizeof(int64_t));
    std::memcpy(flux_buf.buffer.mapped, flux_u32.data(), size_t(anchors_n) * sizeof(uint32_t));
    std::memcpy(harm_buf.buffer.mapped, harm_u32.data(), size_t(anchors_n) * sizeof(uint32_t));
    std::memcpy(ids_buf.buffer.mapped, anchor_ids, size_t(ids_n) * sizeof(uint32_t));
    std::memset(out_buf.buffer.mapped, 0, size_t(ids_n) * sizeof(EwCarrierTriple));

    struct Push {
        uint32_t anchors_n;
        uint32_t ids_n;
    } push{anchors_n, ids_n};

    const std::vector<const EwVkBuffer*> buffers{
        &doppler_buf.buffer,
        &mass_buf.buffer,
        &curv_buf.buffer,
        &flux_buf.buffer,
        &harm_buf.buffer,
        &ids_buf.buffer,
        &out_buf.buffer
    };
    const uint32_t groups = (ids_n + 255u) / 256u;
    if (!ctx.dispatch(EwVkShaderKind::CarrierBundle, buffers, &push, sizeof(push), groups, 1u, 1u)) return false;

    std::memcpy(out_triples, out_buf.buffer.mapped, size_t(ids_n) * sizeof(EwCarrierTriple));
    return true;
}

bool ew_gpu_compute_overlap_scores(
    const EwGpuCarrierRecord& query,
    const EwGpuCarrierRecord* recs,
    size_t n_recs,
    uint32_t lane_filter_u32,
    const uint32_t* opt_domain_id9,
    std::vector<int64_t>& out_scores_q32_32
) {
    out_scores_q32_32.clear();
    if (!recs || n_recs == 0u) return true;

    EwVkContext& ctx = EwVkContext::instance();
    if (!ctx.available()) return false;

    const size_t score_bytes = n_recs * sizeof(int64_t);

    EwVkScopedBuffer query_buf(&ctx);
    EwVkScopedBuffer recs_buf(&ctx);
    EwVkScopedBuffer scores_buf(&ctx);
    EwVkScopedBuffer domain_buf(&ctx);
    if (!ew_gpu_make_buffer(ctx, sizeof(EwGpuCarrierRecord), query_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, n_recs * sizeof(EwGpuCarrierRecord), recs_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, score_bytes, scores_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, 9u * sizeof(uint32_t), domain_buf)) return false;

    std::memcpy(query_buf.buffer.mapped, &query, sizeof(EwGpuCarrierRecord));
    std::memcpy(recs_buf.buffer.mapped, recs, n_recs * sizeof(EwGpuCarrierRecord));
    std::memset(scores_buf.buffer.mapped, 0, score_bytes);
    if (opt_domain_id9) std::memcpy(domain_buf.buffer.mapped, opt_domain_id9, 9u * sizeof(uint32_t));
    else std::memset(domain_buf.buffer.mapped, 0, 9u * sizeof(uint32_t));

    struct Push {
        uint32_t n_u32;
        uint32_t lane_filter_u32;
        uint32_t use_domain_u32;
    } push{};
    push.n_u32 = (n_recs > 0xFFFFFFFFu) ? 0xFFFFFFFFu : uint32_t(n_recs);
    push.lane_filter_u32 = lane_filter_u32;
    push.use_domain_u32 = opt_domain_id9 ? 1u : 0u;

    const std::vector<const EwVkBuffer*> buffers{
        &query_buf.buffer,
        &recs_buf.buffer,
        &scores_buf.buffer,
        &domain_buf.buffer
    };
    const uint32_t groups = (push.n_u32 + 255u) / 256u;
    if (!ctx.dispatch(EwVkShaderKind::OverlapScores, buffers, &push, sizeof(push), groups, 1u, 1u)) return false;

    out_scores_q32_32.resize(n_recs);
    std::memcpy(out_scores_q32_32.data(), scores_buf.buffer.mapped, score_bytes);
    return true;
}

bool ew_gpu_select_topk(
    const int64_t* scores_q32_32,
    size_t n_scores,
    size_t k_in,
    std::vector<GE_OverlapHit>& out_hits
) {
    out_hits.clear();
    if (!scores_q32_32 || n_scores == 0u || k_in == 0u) return true;

    EwVkContext& ctx = EwVkContext::instance();
    if (!ctx.available()) return false;

    const uint32_t k = (k_in > 32u) ? 32u : uint32_t(k_in);
    const uint32_t n = (n_scores > 0xFFFFFFFFu) ? 0xFFFFFFFFu : uint32_t(n_scores);
    const uint32_t chunk = 4096u;
    const uint32_t blocks = std::max<uint32_t>(1u, (n + chunk - 1u) / chunk);

    const size_t list_idx_bytes = size_t(blocks) * size_t(k) * sizeof(uint32_t);
    const size_t list_score_bytes = size_t(blocks) * size_t(k) * sizeof(int64_t);

    EwVkScopedBuffer scores_buf(&ctx);
    EwVkScopedBuffer idx_a_buf(&ctx);
    EwVkScopedBuffer score_a_buf(&ctx);
    EwVkScopedBuffer idx_b_buf(&ctx);
    EwVkScopedBuffer score_b_buf(&ctx);
    if (!ew_gpu_make_buffer(ctx, size_t(n) * sizeof(int64_t), scores_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, list_idx_bytes, idx_a_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, list_score_bytes, score_a_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, list_idx_bytes, idx_b_buf)) return false;
    if (!ew_gpu_make_buffer(ctx, list_score_bytes, score_b_buf)) return false;

    std::memcpy(scores_buf.buffer.mapped, scores_q32_32, size_t(n) * sizeof(int64_t));
    std::memset(idx_a_buf.buffer.mapped, 0xFF, list_idx_bytes);
    std::memset(score_a_buf.buffer.mapped, 0, list_score_bytes);
    std::memset(idx_b_buf.buffer.mapped, 0xFF, list_idx_bytes);
    std::memset(score_b_buf.buffer.mapped, 0, list_score_bytes);

    struct PushBlock {
        uint32_t n_u32;
        uint32_t k_u32;
        uint32_t chunk_u32;
    } push_block{n, k, chunk};

    {
        const std::vector<const EwVkBuffer*> buffers{
            &scores_buf.buffer,
            &idx_a_buf.buffer,
            &score_a_buf.buffer
        };
        if (!ctx.dispatch(EwVkShaderKind::TopkBlock, buffers, &push_block, sizeof(push_block), blocks, 1u, 1u)) return false;
    }

    uint32_t lists = blocks;
    bool flip = false;
    while (lists > 1u) {
        struct PushMerge {
            uint32_t lists_u32;
            uint32_t k_u32;
        } push_merge{lists, k};

        const uint32_t out_lists = (lists + 1u) / 2u;
        const EwVkBuffer* in_idx = flip ? &idx_b_buf.buffer : &idx_a_buf.buffer;
        const EwVkBuffer* in_score = flip ? &score_b_buf.buffer : &score_a_buf.buffer;
        const EwVkBuffer* out_idx = flip ? &idx_a_buf.buffer : &idx_b_buf.buffer;
        const EwVkBuffer* out_score = flip ? &score_a_buf.buffer : &score_b_buf.buffer;

        const std::vector<const EwVkBuffer*> buffers{in_idx, in_score, out_idx, out_score};
        if (!ctx.dispatch(EwVkShaderKind::TopkMerge, buffers, &push_merge, sizeof(push_merge), out_lists, 1u, 1u)) return false;

        lists = out_lists;
        flip = !flip;
    }

    const EwVkBuffer& final_idx = flip ? idx_b_buf.buffer : idx_a_buf.buffer;
    const EwVkBuffer& final_score = flip ? score_b_buf.buffer : score_a_buf.buffer;
    const auto* idx_ptr = reinterpret_cast<const uint32_t*>(final_idx.mapped);
    const auto* score_ptr = reinterpret_cast<const int64_t*>(final_score.mapped);

    out_hits.reserve(k);
    for (uint32_t i = 0; i < k; ++i) {
        const uint32_t idx = idx_ptr[i];
        const int64_t score = score_ptr[i];
        if (idx == kGeTopkSentinelIndex || score <= 0) continue;
        out_hits.push_back({size_t(idx), score});
    }

    std::stable_sort(
        out_hits.begin(),
        out_hits.end(),
        [](const GE_OverlapHit& a, const GE_OverlapHit& b) {
            return ge_better_host(a.score_q32_32, uint32_t(a.record_index), b.score_q32_32, uint32_t(b.record_index));
        });
    return true;
}
