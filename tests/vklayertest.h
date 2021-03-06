/*
 * Copyright (c) 2015-2019 The Khronos Group Inc.
 * Copyright (c) 2015-2019 Valve Corporation
 * Copyright (c) 2015-2019 LunarG, Inc.
 * Copyright (c) 2015-2019 Google, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Author: Chia-I Wu <olvaffe@gmail.com>
 * Author: Chris Forbes <chrisf@ijw.co.nz>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 * Author: Mike Stroyan <mike@LunarG.com>
 * Author: Tobin Ehlis <tobine@google.com>
 * Author: Tony Barbour <tony@LunarG.com>
 * Author: Cody Northrop <cnorthrop@google.com>
 * Author: Dave Houlton <daveh@lunarg.com>
 * Author: Jeremy Kniager <jeremyk@lunarg.com>
 * Author: Shannon McPherson <shannon@lunarg.com>
 * Author: John Zulauf <jzulauf@lunarg.com>
 */

#ifndef VKLAYERTEST_H
#define VKLAYERTEST_H

#ifdef ANDROID
#include "vulkan_wrapper.h"
#else
#define NOMINMAX
#include <vulkan/vulkan.h>
#endif

#include "layers/vk_device_profile_api_layer.h"

#if defined(ANDROID)
#include <android/log.h>
#if defined(VALIDATION_APK)
#include <android_native_app_glue.h>
#endif
#endif

#include "icd-spv.h"
#include "test_common.h"
#include "vk_layer_config.h"
#include "vk_format_utils.h"
#include "vkrenderframework.h"
#include "vk_typemap_helper.h"
#include "convert_to_renderpass2.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <unordered_set>

//--------------------------------------------------------------------------------------
// Mesh and VertexFormat Data
//--------------------------------------------------------------------------------------

static const char kSkipPrefix[] = "             TEST SKIPPED:";

enum BsoFailSelect {
    BsoFailNone,
    BsoFailLineWidth,
    BsoFailDepthBias,
    BsoFailViewport,
    BsoFailScissor,
    BsoFailBlend,
    BsoFailDepthBounds,
    BsoFailStencilReadMask,
    BsoFailStencilWriteMask,
    BsoFailStencilReference,
    BsoFailCmdClearAttachments,
    BsoFailIndexBuffer,
    BsoFailIndexBufferBadSize,
    BsoFailIndexBufferBadOffset,
    BsoFailIndexBufferBadMapSize,
    BsoFailIndexBufferBadMapOffset
};

static const char bindStateVertShaderText[] =
    "#version 450\n"
    "vec2 vertices[3];\n"
    "void main() {\n"
    "      vertices[0] = vec2(-1.0, -1.0);\n"
    "      vertices[1] = vec2( 1.0, -1.0);\n"
    "      vertices[2] = vec2( 0.0,  1.0);\n"
    "   gl_Position = vec4(vertices[gl_VertexIndex % 3], 0.0, 1.0);\n"
    "}\n";

static const char bindStateFragShaderText[] =
    "#version 450\n"
    "\n"
    "layout(location = 0) out vec4 uFragColor;\n"
    "void main(){\n"
    "   uFragColor = vec4(0,1,0,1);\n"
    "}\n";

// Static arrays helper
template <class ElementT, size_t array_size>
size_t size(ElementT (&)[array_size]) {
    return array_size;
}

// Format search helper
VkFormat FindSupportedDepthStencilFormat(VkPhysicalDevice phy);

// Returns true if *any* requested features are available.
// Assumption is that the framework can successfully create an image as
// long as at least one of the feature bits is present (excepting VTX_BUF).
bool ImageFormatIsSupported(VkPhysicalDevice phy, VkFormat format, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
                            VkFormatFeatureFlags features = ~VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);

// Returns true if format and *all* requested features are available.
bool ImageFormatAndFeaturesSupported(VkPhysicalDevice phy, VkFormat format, VkImageTiling tiling, VkFormatFeatureFlags features);

// Returns true if format and *all* requested features are available.
bool ImageFormatAndFeaturesSupported(const VkInstance inst, const VkPhysicalDevice phy, const VkImageCreateInfo info,
                                     const VkFormatFeatureFlags features);

// Validation report callback prototype
VKAPI_ATTR VkBool32 VKAPI_CALL myDbgFunc(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location,
                                         int32_t msgCode, const char *pLayerPrefix, const char *pMsg, void *pUserData);

// Simple sane SamplerCreateInfo boilerplate
VkSamplerCreateInfo SafeSaneSamplerCreateInfo();

VkImageViewCreateInfo SafeSaneImageViewCreateInfo(VkImage image, VkFormat format, VkImageAspectFlags aspect_mask);

VkImageViewCreateInfo SafeSaneImageViewCreateInfo(const VkImageObj &image, VkFormat format, VkImageAspectFlags aspect_mask);

// Helper for checking createRenderPass2 support and adding related extensions.
bool CheckCreateRenderPass2Support(VkRenderFramework *renderFramework, std::vector<const char *> &device_extension_names);

// Helper for checking descriptor_indexing support and adding related extensions.
bool CheckDescriptorIndexingSupportAndInitFramework(VkRenderFramework *renderFramework,
                                                    std::vector<const char *> &instance_extension_names,
                                                    std::vector<const char *> &device_extension_names,
                                                    VkValidationFeaturesEXT *features, void *userData);

// Dependent "false" type for the static assert, as GCC will evaluate
// non-dependent static_asserts even for non-instantiated templates
template <typename T>
struct AlwaysFalse : std::false_type {};

// Helpers to get nearest greater or smaller value (of float) -- useful for testing the boundary cases of Vulkan limits
template <typename T>
T NearestGreater(const T from) {
    using Lim = std::numeric_limits<T>;
    const auto positive_direction = Lim::has_infinity ? Lim::infinity() : Lim::max();

    return std::nextafter(from, positive_direction);
}

template <typename T>
T NearestSmaller(const T from) {
    using Lim = std::numeric_limits<T>;
    const auto negative_direction = Lim::has_infinity ? -Lim::infinity() : Lim::lowest();

    return std::nextafter(from, negative_direction);
}

// ErrorMonitor Usage:
//
// Call SetDesiredFailureMsg with a string to be compared against all
// encountered log messages, or a validation error enum identifying
// desired error message. Passing NULL or VALIDATION_ERROR_MAX_ENUM
// will match all log messages. logMsg will return true for skipCall
// only if msg is matched or NULL.
//
// Call VerifyFound to determine if all desired failure messages
// were encountered. Call VerifyNotFound to determine if any unexpected
// failure was encountered.
class ErrorMonitor {
   public:
    ErrorMonitor();

    ~ErrorMonitor();

    // Set monitor to pristine state
    void Reset();

    // ErrorMonitor will look for an error message containing the specified string(s)
    void SetDesiredFailureMsg(const VkFlags msgFlags, const std::string msg);
    void SetDesiredFailureMsg(const VkFlags msgFlags, const char *const msgString);

    // ErrorMonitor will look for an error message containing the specified string(s)
    template <typename Iter>
    void SetDesiredFailureMsg(const VkFlags msgFlags, Iter iter, const Iter end) {
        for (; iter != end; ++iter) {
            SetDesiredFailureMsg(msgFlags, *iter);
        }
    }

    // Set an error that the error monitor will ignore. Do not use this function if you are creating a new test.
    // TODO: This is stopgap to block new unexpected errors from being introduced. The long-term goal is to remove the use of this
    // function and its definition.
    void SetUnexpectedError(const char *const msg);

    VkBool32 CheckForDesiredMsg(const char *const msgString);
    vector<string> GetOtherFailureMsgs() const;
    VkDebugReportFlagsEXT GetMessageFlags() const;
    bool AnyDesiredMsgFound() const;
    bool AllDesiredMsgsFound() const;
    void SetError(const char *const errorString);
    void SetBailout(bool *bailout);
    void DumpFailureMsgs() const;

    // Helpers

    // ExpectSuccess now takes an optional argument allowing a custom combination of debug flags
    void ExpectSuccess(VkDebugReportFlagsEXT const message_flag_mask = VK_DEBUG_REPORT_ERROR_BIT_EXT);

    void VerifyFound();
    void VerifyNotFound();

   private:
    // TODO: This is stopgap to block new unexpected errors from being introduced. The long-term goal is to remove the use of this
    // function and its definition.
    bool IgnoreMessage(std::string const &msg) const;

    VkFlags message_flags_;
    std::unordered_multiset<std::string> desired_message_strings_;
    std::unordered_multiset<std::string> failure_message_strings_;
    std::vector<std::string> ignore_message_strings_;
    vector<string> other_messages_;
    test_platform_thread_mutex mutex_;
    bool *bailout_;
    bool message_found_;
};

class VkLayerTest : public VkRenderFramework {
   public:
    void VKTriangleTest(BsoFailSelect failCase);

    void GenericDrawPreparation(VkCommandBufferObj *commandBuffer, VkPipelineObj &pipelineobj, VkDescriptorSetObj &descriptorSet,
                                BsoFailSelect failCase);

    void Init(VkPhysicalDeviceFeatures *features = nullptr, VkPhysicalDeviceFeatures2 *features2 = nullptr,
              const VkCommandPoolCreateFlags flags = 0, void *instance_pnext = nullptr);
    ErrorMonitor *Monitor();
    VkCommandBufferObj *CommandBuffer();

   protected:
    ErrorMonitor *m_errorMonitor;
    uint32_t m_instance_api_version = 0;
    uint32_t m_target_api_version = 0;
    bool m_enableWSI;
    virtual void SetUp();
    uint32_t SetTargetApiVersion(uint32_t target_api_version);
    uint32_t DeviceValidationVersion();
    bool LoadDeviceProfileLayer(
        PFN_vkSetPhysicalDeviceFormatPropertiesEXT &fpvkSetPhysicalDeviceFormatPropertiesEXT,
        PFN_vkGetOriginalPhysicalDeviceFormatPropertiesEXT &fpvkGetOriginalPhysicalDeviceFormatPropertiesEXT);
    virtual void TearDown();
    VkLayerTest();
};

class VkPositiveLayerTest : public VkLayerTest {
   public:
   protected:
};

class VkWsiEnabledLayerTest : public VkLayerTest {
   public:
   protected:
    VkWsiEnabledLayerTest() { m_enableWSI = true; }
};

class VkBufferTest {
   public:
    enum eTestEnFlags {
        eDoubleDelete,
        eInvalidDeviceOffset,
        eInvalidMemoryOffset,
        eBindNullBuffer,
        eBindFakeBuffer,
        eFreeInvalidHandle,
        eNone,
    };

    enum eTestConditions { eOffsetAlignment = 1 };

    static bool GetTestConditionValid(VkDeviceObj *aVulkanDevice, eTestEnFlags aTestFlag, VkBufferUsageFlags aBufferUsage = 0);
    // A constructor which performs validation tests within construction.
    VkBufferTest(VkDeviceObj *aVulkanDevice, VkBufferUsageFlags aBufferUsage, eTestEnFlags aTestFlag = eNone);
    ~VkBufferTest();
    bool GetBufferCurrent();
    const VkBuffer &GetBuffer();
    void TestDoubleDestroy();

   protected:
    bool AllocateCurrent;
    bool BoundCurrent;
    bool CreateCurrent;
    bool InvalidDeleteEn;

    VkBuffer VulkanBuffer;
    VkDevice VulkanDevice;
    VkDeviceMemory VulkanMemory;
};

class VkVerticesObj {
   public:
    VkVerticesObj(VkDeviceObj *aVulkanDevice, unsigned aAttributeCount, unsigned aBindingCount, unsigned aByteStride,
                  VkDeviceSize aVertexCount, const float *aVerticies);
    ~VkVerticesObj();
    bool AddVertexInputToPipe(VkPipelineObj &aPipelineObj);
    void BindVertexBuffers(VkCommandBuffer aCommandBuffer, unsigned aOffsetCount = 0, VkDeviceSize *aOffsetList = nullptr);

   protected:
    static uint32_t BindIdGenerator;

    bool BoundCurrent;
    unsigned AttributeCount;
    unsigned BindingCount;
    uint32_t BindId;

    VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo;
    VkVertexInputAttributeDescription *VertexInputAttributeDescription;
    VkVertexInputBindingDescription *VertexInputBindingDescription;
    VkConstantBufferObj VulkanMemoryBuffer;
};

struct OneOffDescriptorSet {
    VkDeviceObj *device_;
    VkDescriptorPool pool_;
    VkDescriptorSetLayoutObj layout_;
    VkDescriptorSet set_;
    typedef std::vector<VkDescriptorSetLayoutBinding> Bindings;

    OneOffDescriptorSet(VkDeviceObj *device, const Bindings &bindings, VkDescriptorSetLayoutCreateFlags layout_flags = 0,
                        void *layout_pnext = NULL, VkDescriptorPoolCreateFlags poolFlags = 0, void *allocate_pnext = NULL);
    ~OneOffDescriptorSet();
    bool Initialized();
};

template <typename T>
bool IsValidVkStruct(const T &s) {
    return LvlTypeMap<T>::kSType == s.sType;
}

// Helper class for tersely creating create pipeline tests
//
// Designed with minimal error checking to ensure easy error state creation
// See OneshotTest for typical usage
struct CreatePipelineHelper {
   public:
    std::vector<VkDescriptorSetLayoutBinding> dsl_bindings_;
    std::unique_ptr<OneOffDescriptorSet> descriptor_set_;
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_;
    VkPipelineVertexInputStateCreateInfo vi_ci_ = {};
    VkPipelineInputAssemblyStateCreateInfo ia_ci_ = {};
    VkPipelineTessellationStateCreateInfo tess_ci_ = {};
    VkViewport viewport_ = {};
    VkRect2D scissor_ = {};
    VkPipelineViewportStateCreateInfo vp_state_ci_ = {};
    VkPipelineMultisampleStateCreateInfo pipe_ms_state_ci_ = {};
    VkPipelineLayoutCreateInfo pipeline_layout_ci_ = {};
    VkPipelineLayoutObj pipeline_layout_;
    VkPipelineDynamicStateCreateInfo dyn_state_ci_ = {};
    VkPipelineRasterizationStateCreateInfo rs_state_ci_ = {};
    VkPipelineColorBlendAttachmentState cb_attachments_ = {};
    VkPipelineColorBlendStateCreateInfo cb_ci_ = {};
    VkGraphicsPipelineCreateInfo gp_ci_ = {};
    VkPipelineCacheCreateInfo pc_ci_ = {};
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::unique_ptr<VkShaderObj> vs_;
    std::unique_ptr<VkShaderObj> fs_;
    VkLayerTest &layer_test_;
    CreatePipelineHelper(VkLayerTest &test);
    ~CreatePipelineHelper();

    void InitDescriptorSetInfo();
    void InitInputAndVertexInfo();
    void InitMultisampleInfo();
    void InitPipelineLayoutInfo();
    void InitViewportInfo();
    void InitDynamicStateInfo();
    void InitShaderInfo();
    void InitRasterizationInfo();
    void InitBlendStateInfo();
    void InitGraphicsPipelineInfo();
    void InitPipelineCacheInfo();

    // Not called by default during init_info
    void InitTesselationState();

    // TDB -- add control for optional and/or additional initialization
    void InitInfo();
    void InitState();
    void LateBindPipelineInfo();
    VkResult CreateGraphicsPipeline(bool implicit_destroy = true, bool do_late_bind = true);

    // Helper function to create a simple test case (positive or negative)
    //
    // info_override can be any callable that takes a CreatePipelineHeper &
    // flags, error can be any args accepted by "SetDesiredFailure".
    template <typename Test, typename OverrideFunc, typename Error>
    static void OneshotTest(Test &test, OverrideFunc &info_override, const VkFlags flags, const std::vector<Error> &errors,
                            bool positive_test = false) {
        CreatePipelineHelper helper(test);
        helper.InitInfo();
        info_override(helper);
        helper.InitState();

        for (const auto &error : errors) test.Monitor()->SetDesiredFailureMsg(flags, error);
        helper.CreateGraphicsPipeline();

        if (positive_test) {
            test.Monitor()->VerifyNotFound();
        } else {
            test.Monitor()->VerifyFound();
        }
    }

    template <typename Test, typename OverrideFunc, typename Error>
    static void OneshotTest(Test &test, OverrideFunc &info_override, const VkFlags flags, Error error, bool positive_test = false) {
        OneshotTest(test, info_override, flags, std::vector<Error>(1, error), positive_test);
    }
};

// Helper class for tersely creating create ray tracing pipeline tests
//
// Designed with minimal error checking to ensure easy error state creation
// See OneshotTest for typical usage
struct CreateNVRayTracingPipelineHelper {
   public:
    std::vector<VkDescriptorSetLayoutBinding> dsl_bindings_;
    std::unique_ptr<OneOffDescriptorSet> descriptor_set_;
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_;
    VkPipelineLayoutCreateInfo pipeline_layout_ci_ = {};
    VkPipelineLayoutObj pipeline_layout_;
    VkRayTracingPipelineCreateInfoNV rp_ci_ = {};
    VkPipelineCacheCreateInfo pc_ci_ = {};
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::vector<VkRayTracingShaderGroupCreateInfoNV> groups_;
    std::unique_ptr<VkShaderObj> rgs_;
    std::unique_ptr<VkShaderObj> chs_;
    std::unique_ptr<VkShaderObj> mis_;
    VkLayerTest &layer_test_;
    CreateNVRayTracingPipelineHelper(VkLayerTest &test);
    ~CreateNVRayTracingPipelineHelper();

    static bool InitInstanceExtensions(VkLayerTest &test, std::vector<const char *> &instance_extension_names);
    static bool InitDeviceExtensions(VkLayerTest &test, std::vector<const char *> &device_extension_names);
    void InitShaderGroups();
    void InitDescriptorSetInfo();
    void InitPipelineLayoutInfo();
    void InitShaderInfo();
    void InitNVRayTracingPipelineInfo();
    void InitPipelineCacheInfo();
    void InitInfo();
    void InitState();
    void LateBindPipelineInfo();
    VkResult CreateNVRayTracingPipeline(bool implicit_destroy = true, bool do_late_bind = true);

    // Helper function to create a simple test case (positive or negative)
    //
    // info_override can be any callable that takes a CreateNVRayTracingPipelineHelper &
    // flags, error can be any args accepted by "SetDesiredFailure".
    template <typename Test, typename OverrideFunc, typename Error>
    static void OneshotTest(Test &test, OverrideFunc &info_override, const std::vector<Error> &errors,
                            const VkFlags flags = VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        CreateNVRayTracingPipelineHelper helper(test);
        helper.InitInfo();
        info_override(helper);
        helper.InitState();

        for (const auto &error : errors) test.Monitor()->SetDesiredFailureMsg(flags, error);
        helper.CreateNVRayTracingPipeline();
        test.Monitor()->VerifyFound();
    }

    template <typename Test, typename OverrideFunc, typename Error>
    static void OneshotTest(Test &test, OverrideFunc &info_override, Error error,
                            const VkFlags flags = VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        OneshotTest(test, info_override, std::vector<Error>(1, error), flags);
    }

    template <typename Test, typename OverrideFunc>
    static void OneshotPositiveTest(Test &test, OverrideFunc &info_override,
                                    const VkDebugReportFlagsEXT message_flag_mask = VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        CreateNVRayTracingPipelineHelper helper(test);
        helper.InitInfo();
        info_override(helper);
        helper.InitState();

        test.Monitor()->ExpectSuccess(message_flag_mask);
        ASSERT_VK_SUCCESS(helper.CreateNVRayTracingPipeline());
        test.Monitor()->VerifyNotFound();
    }
};

namespace chain_util {
template <typename T>
T Init(const void *pnext_in = nullptr) {
    T pnext_obj = {};
    pnext_obj.sType = LvlTypeMap<T>::kSType;
    pnext_obj.pNext = pnext_in;
    return pnext_obj;
}

class ExtensionChain {
    const void *head_ = nullptr;
    typedef std::function<bool(const char *)> AddIfFunction;
    AddIfFunction add_if_;
    typedef std::vector<const char *> List;
    List *list_;

   public:
    template <typename F>
    ExtensionChain(F &add_if, List *list) : add_if_(add_if), list_(list) {}

    template <typename T>
    void Add(const char *name, T &obj) {
        if (add_if_(name)) {
            if (list_) {
                list_->push_back(name);
            }
            obj.pNext = head_;
            head_ = &obj;
        }
    }

    const void *Head() const;
};
}  // namespace chain_util

// PushDescriptorProperties helper
VkPhysicalDevicePushDescriptorPropertiesKHR GetPushDescriptorProperties(VkInstance instance, VkPhysicalDevice gpu);

class BarrierQueueFamilyTestHelper {
   public:
    struct QueueFamilyObjs {
        uint32_t index;
        // We would use std::unique_ptr, but this triggers a compiler error on older compilers
        VkQueueObj *queue = nullptr;
        VkCommandPoolObj *command_pool = nullptr;
        VkCommandBufferObj *command_buffer = nullptr;
        VkCommandBufferObj *command_buffer2 = nullptr;
        ~QueueFamilyObjs();
        void Init(VkDeviceObj *device, uint32_t qf_index, VkQueue qf_queue, VkCommandPoolCreateFlags cp_flags);
    };

    struct Context {
        VkLayerTest *layer_test;
        uint32_t default_index;
        std::unordered_map<uint32_t, QueueFamilyObjs> queue_families;
        Context(VkLayerTest *test, const std::vector<uint32_t> &queue_family_indices);
        void Reset();
    };

    BarrierQueueFamilyTestHelper(Context *context);
    // Init with queue families non-null for CONCURRENT sharing mode (which requires them)
    void Init(std::vector<uint32_t> *families);

    QueueFamilyObjs *GetQueueFamilyInfo(Context *context, uint32_t qfi);

    enum Modifier {
        NONE,
        DOUBLE_RECORD,
        DOUBLE_COMMAND_BUFFER,
    };

    void operator()(std::string img_err, std::string buf_err, uint32_t src, uint32_t dst, bool positive = false,
                    uint32_t queue_family_index = kInvalidQueueFamily, Modifier mod = Modifier::NONE);

   protected:
    static const uint32_t kInvalidQueueFamily = UINT32_MAX;
    Context *context_;
    VkImageObj image_;
    VkImageMemoryBarrier image_barrier_;
    VkBufferObj buffer_;
    VkBufferMemoryBarrier buffer_barrier_;
};

struct DebugUtilsLabelCheckData {
    std::function<void(const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, DebugUtilsLabelCheckData *)> callback;
    size_t count;
};

bool operator==(const VkDebugUtilsLabelEXT &rhs, const VkDebugUtilsLabelEXT &lhs);

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData);

#if GTEST_IS_THREADSAFE
struct thread_data_struct {
    VkCommandBuffer commandBuffer;
    VkDevice device;
    VkEvent event;
    bool bailout;
};

extern "C" void *AddToCommandBuffer(void *arg);
#endif  // GTEST_IS_THREADSAFE

extern "C" void *ReleaseNullFence(void *arg);

void TestRenderPassCreate(ErrorMonitor *error_monitor, const VkDevice device, const VkRenderPassCreateInfo *create_info,
                          bool rp2Supported, const char *rp1_vuid, const char *rp2_vuid);

void TestRenderPassBegin(ErrorMonitor *error_monitor, const VkDevice device, const VkCommandBuffer command_buffer,
                         const VkRenderPassBeginInfo *begin_info, bool rp2Supported, const char *rp1_vuid, const char *rp2_vuid);

// Helpers for the tests below
void ValidOwnershipTransferOp(ErrorMonitor *monitor, VkCommandBufferObj *cb, VkPipelineStageFlags src_stages,
                              VkPipelineStageFlags dst_stages, const VkBufferMemoryBarrier *buf_barrier,
                              const VkImageMemoryBarrier *img_barrier);

void ValidOwnershipTransfer(ErrorMonitor *monitor, VkCommandBufferObj *cb_from, VkCommandBufferObj *cb_to,
                            VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
                            const VkBufferMemoryBarrier *buf_barrier, const VkImageMemoryBarrier *img_barrier);

VkResult GPDIFPHelper(VkPhysicalDevice dev, const VkImageCreateInfo *ci, VkImageFormatProperties *limits = nullptr);

VkFormat FindFormatLinearWithoutMips(VkPhysicalDevice gpu, VkImageCreateInfo image_ci);

bool FindFormatWithoutSamples(VkPhysicalDevice gpu, VkImageCreateInfo &image_ci);

bool FindUnsupportedImage(VkPhysicalDevice gpu, VkImageCreateInfo &image_ci);

VkFormat FindFormatWithoutFeatures(VkPhysicalDevice gpu, VkImageTiling tiling,
                                   VkFormatFeatureFlags undesired_features = UINT32_MAX);

void NegHeightViewportTests(VkDeviceObj *m_device, VkCommandBufferObj *m_commandBuffer, ErrorMonitor *m_errorMonitor);

#endif  // VKLAYERTEST_H
