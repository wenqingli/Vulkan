/*
* Vulkan Example - imGui (https://github.com/ocornut/imgui)
*
* Copyright (C) 2017-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <imgui.h>
#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

// Options and values to display/toggle from the UI
struct UISettings {
	bool displayModels = true;
	bool displayLogos = true;
	bool displayBackground = true;
	bool animateLight = false;
	float lightSpeed = 0.25f;
	std::array<float, 50> frameTimes{};
	float frameTimeMin = 9999.0f, frameTimeMax = 0.0f;
	float lightTimer = 0.0f;
} uiSettings;

// ----------------------------------------------------------------------------
// ImGUI class
// ----------------------------------------------------------------------------
class ImGUI {
private:
	// Vulkan resources for rendering the UI
	VkSampler sampler;
	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	int32_t vertexCount = 0;
	int32_t indexCount = 0;
	VkDeviceMemory fontMemory = VK_NULL_HANDLE;
	VkImage fontImage = VK_NULL_HANDLE;
	VkImageView fontView = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	vks::VulkanDevice *device;
	VkPhysicalDeviceDriverProperties driverProperties = {};
	VulkanExampleBase *example;
	ImGuiStyle vulkanStyle;
	int selectedStyle = 0;
public:
	// UI params are set via push constants
	struct PushConstBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstBlock;

	ImGUI(VulkanExampleBase *example) : example(example)
	{
		device = example->vulkanDevice;
		ImGui::CreateContext();
		
		//SRS - Set ImGui font and style scale factors to handle retina and other HiDPI displays
		ImGuiIO& io = ImGui::GetIO();
		io.FontGlobalScale = example->ui.scale;
		ImGuiStyle& style = ImGui::GetStyle();
		style.ScaleAllSizes(example->ui.scale);
	};

	~ImGUI()
	{
		ImGui::DestroyContext();
		// Release all Vulkan resources required for rendering imGui
		vertexBuffer.destroy();
		indexBuffer.destroy();
		vkDestroyImage(device->logicalDevice, fontImage, nullptr);
		vkDestroyImageView(device->logicalDevice, fontView, nullptr);
		vkFreeMemory(device->logicalDevice, fontMemory, nullptr);
		vkDestroySampler(device->logicalDevice, sampler, nullptr);
		vkDestroyPipelineCache(device->logicalDevice, pipelineCache, nullptr);
		vkDestroyPipeline(device->logicalDevice, pipeline, nullptr);
		vkDestroyPipelineLayout(device->logicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayout, nullptr);
	}

	// Initialize styles, keys, etc.
	void init(float width, float height)
	{
		// Color scheme
		vulkanStyle = ImGui::GetStyle();
		vulkanStyle.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
		vulkanStyle.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
		vulkanStyle.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		vulkanStyle.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
		vulkanStyle.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

		setStyle(0);
		
		// Dimensions
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(width, height);
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
#if defined(_WIN32)
		// If we directly work with os specific key codes, we need to map special key types like tab
		io.KeyMap[ImGuiKey_Tab] = VK_TAB;
		io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
		io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
		io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
		io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
		io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
		io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
		io.KeyMap[ImGuiKey_Space] = VK_SPACE;
		io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
#endif
	}

	void setStyle(uint32_t index)
	{
		switch (index)
		{
		case 0:
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style = vulkanStyle;
			break;
		}
		case 1:
			ImGui::StyleColorsClassic();
			break;
		case 2:
			ImGui::StyleColorsDark();
			break;
		case 3:
			ImGui::StyleColorsLight();
			break;
		}
	}

	// Initialize all Vulkan resources used by the ui
	void initResources(VkRenderPass renderPass, VkQueue copyQueue, const std::string& shadersPath)
	{
		ImGuiIO& io = ImGui::GetIO();

		// Create font texture
		unsigned char* fontData;
		int texWidth, texHeight;
		io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
		VkDeviceSize uploadSize = texWidth*texHeight * 4 * sizeof(char);

		//SRS - Get Vulkan device driver information if available, use later for display
		if (device->extensionSupported(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
		{
			VkPhysicalDeviceProperties2 deviceProperties2 = {};
			deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
			deviceProperties2.pNext = &driverProperties;
			driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
			vkGetPhysicalDeviceProperties2(device->physicalDevice, &deviceProperties2);
		}

		// Create target image for copy
		VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.extent.width = texWidth;
		imageInfo.extent.height = texHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageInfo, nullptr, &fontImage));
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device->logicalDevice, fontImage, &memReqs);
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &fontMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, fontImage, fontMemory, 0));

		// Image view
		VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
		viewInfo.image = fontImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &fontView));

		// Staging buffers for font data upload
		vks::Buffer stagingBuffer;

		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			uploadSize));

		stagingBuffer.map();
		memcpy(stagingBuffer.mapped, fontData, uploadSize);
		stagingBuffer.unmap();

		// Copy buffer data to font image
		VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Prepare for transfer
		vks::tools::setImageLayout(
			copyCmd,
			fontImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT);

		// Copy
		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = texWidth;
		bufferCopyRegion.imageExtent.height = texHeight;
		bufferCopyRegion.imageExtent.depth = 1;

		vkCmdCopyBufferToImage(
			copyCmd,
			stagingBuffer.buffer,
			fontImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&bufferCopyRegion
		);

		// Prepare for shader read
		vks::tools::setImageLayout(
			copyCmd,
			fontImage,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		device->flushCommandBuffer(copyCmd, copyQueue, true);

		stagingBuffer.destroy();

		// Font texture Sampler
		VkSamplerCreateInfo samplerInfo = vks::initializers::samplerCreateInfo();
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

		// Descriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Descriptor set layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Descriptor set
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &allocInfo, &descriptorSet));
		VkDescriptorImageInfo fontDescriptor = vks::initializers::descriptorImageInfo(
			sampler,
			fontView,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &fontDescriptor)
		};
		vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Pipeline cache
		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(device->logicalDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));

		// Pipeline layout
		// Push constants for UI rendering parameters
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(PushConstBlock), 0);
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device->logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Setup graphics pipeline for UI rendering
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

		// Enable blending
		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		// Vertex bindings an attributes based on ImGui vertex definition
		std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)),	// Location 0: Position
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)),	// Location 1: UV
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)),	// Location 0: Color
		};
		VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
		vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

		pipelineCreateInfo.pVertexInputState = &vertexInputState;

		shaderStages[0] = example->loadShader(shadersPath + "imgui/ui.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = example->loadShader(shadersPath + "imgui/ui.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device->logicalDevice, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
	}

	// Starts a new imGui frame and sets up windows and ui elements
	void newFrame(VulkanExampleBase *example, bool updateFrameGraph)
	{
		ImGui::NewFrame();

		// Init imGui windows and elements

		// Debug window
		ImGui::SetWindowPos(ImVec2(20 * example->ui.scale, 20 * example->ui.scale), ImGuiSetCond_FirstUseEver);
        ImGui::SetWindowSize(ImVec2(300 * example->ui.scale, 300 * example->ui.scale), ImGuiSetCond_Always);
		ImGui::TextUnformatted(example->title.c_str());
		ImGui::TextUnformatted(device->properties.deviceName);
		
		//SRS - Display Vulkan API version and device driver information if available (otherwise blank)
		ImGui::Text("Vulkan API %i.%i.%i", VK_API_VERSION_MAJOR(device->properties.apiVersion), VK_API_VERSION_MINOR(device->properties.apiVersion), VK_API_VERSION_PATCH(device->properties.apiVersion));
		ImGui::Text("%s %s", driverProperties.driverName, driverProperties.driverInfo);

		// Update frame time display
		if (updateFrameGraph) {
			std::rotate(uiSettings.frameTimes.begin(), uiSettings.frameTimes.begin() + 1, uiSettings.frameTimes.end());
			float frameTime = 1000.0f / (example->frameTimer * 1000.0f);
			uiSettings.frameTimes.back() = frameTime;
			if (frameTime < uiSettings.frameTimeMin) {
				uiSettings.frameTimeMin = frameTime;
			}
			if (frameTime > uiSettings.frameTimeMax) {
				uiSettings.frameTimeMax = frameTime;
			}
		}

		ImGui::PlotLines("Frame Times", &uiSettings.frameTimes[0], 50, 0, "", uiSettings.frameTimeMin, uiSettings.frameTimeMax, ImVec2(0, 80));

		ImGui::Text("Camera");
		ImGui::InputFloat3("position", &example->camera.position.x, 2);
		ImGui::InputFloat3("rotation", &example->camera.rotation.x, 2);

		// Example settings window
		ImGui::SetNextWindowPos(ImVec2(20 * example->ui.scale, 360 * example->ui.scale), ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300 * example->ui.scale, 200 * example->ui.scale), ImGuiSetCond_FirstUseEver);
		ImGui::Begin("Example settings");
		ImGui::Checkbox("Render models", &uiSettings.displayModels);
		ImGui::Checkbox("Display logos", &uiSettings.displayLogos);
		ImGui::Checkbox("Display background", &uiSettings.displayBackground);
		ImGui::Checkbox("Animate light", &uiSettings.animateLight);
		ImGui::SliderFloat("Light speed", &uiSettings.lightSpeed, 0.1f, 1.0f);
		//ImGui::ShowStyleSelector("UI style");

		if (ImGui::Combo("UI style", &selectedStyle, "Vulkan\0Classic\0Dark\0Light\0")) {
			setStyle(selectedStyle);
		}

		ImGui::End();

		//SRS - ShowDemoWindow() sets its own initial position and size, cannot override here
		ImGui::ShowDemoWindow();

		// Render to generate draw buffers
		ImGui::Render();
	}

	// Update vertex and index buffer containing the imGui elements when required
	void updateBuffers()
	{
		ImDrawData* imDrawData = ImGui::GetDrawData();

		// Note: Alignment is done inside buffer creation
		VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		if ((vertexBufferSize == 0) || (indexBufferSize == 0)) {
			return;
		}

		// Update buffers only if vertex or index count has been changed compared to current buffer size

		// Vertex buffer
		if ((vertexBuffer.buffer == VK_NULL_HANDLE) || (vertexCount != imDrawData->TotalVtxCount)) {
			vertexBuffer.unmap();
			vertexBuffer.destroy();
			VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &vertexBuffer, vertexBufferSize));
			vertexCount = imDrawData->TotalVtxCount;
			vertexBuffer.map();
		}

		// Index buffer
		if ((indexBuffer.buffer == VK_NULL_HANDLE) || (indexCount < imDrawData->TotalIdxCount)) {
			indexBuffer.unmap();
			indexBuffer.destroy();
			VK_CHECK_RESULT(device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &indexBuffer, indexBufferSize));
			indexCount = imDrawData->TotalIdxCount;
			indexBuffer.map();
		}

		// Upload data
		ImDrawVert* vtxDst = (ImDrawVert*)vertexBuffer.mapped;
		ImDrawIdx* idxDst = (ImDrawIdx*)indexBuffer.mapped;

		for (int n = 0; n < imDrawData->CmdListsCount; n++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];
			memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmd_list->VtxBuffer.Size;
			idxDst += cmd_list->IdxBuffer.Size;
		}

		// Flush to make writes visible to GPU
		vertexBuffer.flush();
		indexBuffer.flush();
	}

	// Draw current imGui frame into a command buffer
	void drawFrame(VkCommandBuffer commandBuffer)
	{
		ImGuiIO& io = ImGui::GetIO();

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkViewport viewport = vks::initializers::viewport(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

		// UI scale and translate via push constants
		pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
		pushConstBlock.translate = glm::vec2(-1.0f);
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

		// Render commands
		ImDrawData* imDrawData = ImGui::GetDrawData();
		int32_t vertexOffset = 0;
		int32_t indexOffset = 0;

		if (imDrawData->CmdListsCount > 0) {

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);

			for (int32_t i = 0; i < imDrawData->CmdListsCount; i++)
			{
				const ImDrawList* cmd_list = imDrawData->CmdLists[i];
				for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
				{
					const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
					VkRect2D scissorRect;
					scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
					scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
					scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
					scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
					vkCmdSetScissor(commandBuffer, 0, 1, &scissorRect);
					vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
					indexOffset += pcmd->ElemCount;
				}
#if (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_METAL_EXT)) && TARGET_OS_SIMULATOR
				// Apple Device Simulator does not support vkCmdDrawIndexed() with vertexOffset > 0, so rebind vertex buffer instead
				offsets[0] += cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, offsets);
#else
				vertexOffset += cmd_list->VtxBuffer.Size;
#endif
			}
		}
	}

};

// ----------------------------------------------------------------------------
// VulkanExample
// ----------------------------------------------------------------------------

class VulkanExample : public VulkanExampleBase
{
public:
	ImGUI *imGui = nullptr;

	struct Models {
		vkglTF::Model models;
		vkglTF::Model logos;
		vkglTF::Model background;
	} models;

	vks::Buffer uniformBufferVS;

	struct UBOVS {
		glm::mat4 projection;
		glm::mat4 modelview;
		glm::vec4 lightPos;
	} uboVS;

	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;

	VulkanExample() : VulkanExampleBase()
	{
		title = "User interfaces with ImGui";
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -4.8f));
		camera.setRotation(glm::vec3(4.5f, -380.0f, 0.0f));
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
		
		//SRS - Enable VK_KHR_get_physical_device_properties2 to retrieve device driver information for display
		enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

		// Don't use the ImGui overlay of the base framework in this sample
		settings.overlay = false;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		uniformBufferVS.destroy();

		delete imGui;
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.2f, 0.2f, 0.2f, 1.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		imGui->newFrame(this, (frameCounter == 0));
		imGui->updateBuffers();

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			// Render scene
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			VkDeviceSize offsets[1] = { 0 };
			if (uiSettings.displayBackground) {
				models.background.draw(drawCmdBuffers[i]);
			}

			if (uiSettings.displayModels) {
				models.models.draw(drawCmdBuffers[i]);
			}

			if (uiSettings.displayLogos) {
				models.logos.draw(drawCmdBuffers[i]);
			}

			// Render imGui
			if (ui.visible) {
				imGui->drawFrame(drawCmdBuffers[i]);
			}

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void setupLayoutsAndDescriptors()
	{
		// descriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Set layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Pipeline layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Descriptor set
		VkDescriptorSetAllocateInfo allocInfo =	vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBufferVS.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void preparePipelines()
	{
		// Rendering
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0,	VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		std::array<VkPipelineShaderStageCreateInfo,2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color });;

		shaderStages[0] = loadShader(getShadersPath() + "imgui/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "imgui/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBufferVS,
			sizeof(uboVS),
			&uboVS));

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Vertex shader
		uboVS.projection = camera.matrices.perspective;
		uboVS.modelview = camera.matrices.view * glm::mat4(1.0f);

		// Light source
		if (uiSettings.animateLight) {
			uiSettings.lightTimer += frameTimer * uiSettings.lightSpeed;
			uboVS.lightPos.x = sin(glm::radians(uiSettings.lightTimer * 360.0f)) * 15.0f;
			uboVS.lightPos.z = cos(glm::radians(uiSettings.lightTimer * 360.0f)) * 15.0f;
		};

		VK_CHECK_RESULT(uniformBufferVS.map());
		memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
		uniformBufferVS.unmap();
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		buildCommandBuffers();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.models.loadFromFile(getAssetPath() + "models/vulkanscenemodels.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.background.loadFromFile(getAssetPath() + "models/vulkanscenebackground.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.logos.loadFromFile(getAssetPath() + "models/vulkanscenelogos.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void prepareImGui()
	{
		imGui = new ImGUI(this);
		imGui->init((float)width, (float)height);
		imGui->initResources(renderPass, queue, getShadersPath());
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		setupLayoutsAndDescriptors();
		preparePipelines();
		prepareImGui();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;

		updateUniformBuffers();

		// Update imGui
		ImGuiIO& io = ImGui::GetIO();

		io.DisplaySize = ImVec2((float)width, (float)height);
		io.DeltaTime = frameTimer;

		io.MousePos = ImVec2(mouseState.position.x, mouseState.position.y);
		io.MouseDown[0] = mouseState.buttons.left && ui.visible;
		io.MouseDown[1] = mouseState.buttons.right && ui.visible;
		io.MouseDown[2] = mouseState.buttons.middle && ui.visible;

		draw();
	}

	virtual void mouseMoved(double x, double y, bool &handled)
	{
		ImGuiIO& io = ImGui::GetIO();
		handled = io.WantCaptureMouse && ui.visible;
	}

// Input handling is platform specific, to show how it's basically done this sample implements it for Windows
#if defined(_WIN32)
	virtual void OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		ImGuiIO& io = ImGui::GetIO();
		// Only react to keyboard input if ImGui is active
		if (io.WantCaptureKeyboard) {
			// Character input
			if (uMsg == WM_CHAR) {
				if (wParam > 0 && wParam < 0x10000) {
					io.AddInputCharacter((unsigned short)wParam);
				}
			}
			// Special keys (tab, cursor, etc.)
			if ((wParam < 256) && (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN)) {
				io.KeysDown[wParam] = true;
			}
			if ((wParam < 256) && (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)) {
				io.KeysDown[wParam] = false;
			}
		}
	}
#endif

};

VULKAN_EXAMPLE_MAIN()
