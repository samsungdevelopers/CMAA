/* Copyright (c) 2020, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cmaa.h"

#include "common/vk_common.h"
#include "gltf_loader.h"
#include "gui.h"
#include "platform/filesystem.h"
#include "platform/platform.h"
#include "rendering/postprocessing_renderpass.h"
#include "rendering/postprocessing_computepass.h"
#include "rendering/subpasses/forward_subpass.h"
#include "stats/stats.h"

namespace
{
const std::string to_string(VkSampleCountFlagBits count)
{
	switch (count)
	{
		case VK_SAMPLE_COUNT_1_BIT:
			return "No MSAA";
		case VK_SAMPLE_COUNT_2_BIT:
			return "2X MSAA";
		case VK_SAMPLE_COUNT_4_BIT:
			return "4X MSAA";
		case VK_SAMPLE_COUNT_8_BIT:
			return "8X MSAA";
		case VK_SAMPLE_COUNT_16_BIT:
			return "16X MSAA";
		case VK_SAMPLE_COUNT_32_BIT:
			return "32X MSAA";
		case VK_SAMPLE_COUNT_64_BIT:
			return "64X MSAA";
		default:
			return "Unknown";
	}
}

const std::string to_string(VkResolveModeFlagBits mode)
{
	switch (mode)
	{
		case VK_RESOLVE_MODE_NONE:
			return "None";
		case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:
			return "Sample 0";
		case VK_RESOLVE_MODE_AVERAGE_BIT:
			return "Average";
		case VK_RESOLVE_MODE_MIN_BIT:
			return "Min";
		case VK_RESOLVE_MODE_MAX_BIT:
			return "Max";
		default:
			return "Unknown";
	}
}
}        // namespace

CMAASample::CMAASample()
{
	// Extension of interest in this sample (optional)
	add_device_extension(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, true);

	// Extension dependency requirements (given that instance API version is 1.0.0)
	add_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, true);
	add_device_extension(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, true);
	add_device_extension(VK_KHR_MAINTENANCE2_EXTENSION_NAME, true);
	add_device_extension(VK_KHR_MULTIVIEW_EXTENSION_NAME, true);

	auto &config = get_configuration();

	// MSAA will be enabled by default if supported
	// Batch mode will test the toggle between 1 or 2 renderpasses
	// with writeback resolve of color and depth
	config.insert<vkb::BoolSetting>(0, gui_run_postprocessing, false);
	config.insert<vkb::BoolSetting>(1, gui_run_postprocessing, true);
}

bool CMAASample::prepare(vkb::Platform &platform)
{
	if (!VulkanSample::prepare(platform))
	{
		return false;
	}

    prepare_supported_sample_count_list();

	depth_writeback_resolve_supported = device->is_enabled(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
	if (depth_writeback_resolve_supported)
	{
		prepare_depth_resolve_mode_list();
	}

	//load_scene("scenes/bonza/Bonza.gltf");
    load_scene("scenes/space_module/SpaceModule.gltf");

	auto &camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
	camera            = dynamic_cast<vkb::sg::PerspectiveCamera *>(&camera_node.get_component<vkb::sg::Camera>());

	vkb::ShaderSource scene_vs("base.vert");
	vkb::ShaderSource scene_fs("base.frag");
	auto              scene_subpass = std::make_unique<vkb::ForwardSubpass>(get_render_context(), std::move(scene_vs), std::move(scene_fs), *scene, *camera);
	scene_pipeline                  = std::make_unique<vkb::RenderPipeline>();
	scene_pipeline->add_subpass(std::move(scene_subpass));

	vkb::ShaderSource postprocessing_vs("postprocessing/postprocessing.vert");
    vkb::ShaderSource CMAA_vs("postprocessing/CMAA.vert");

    postprocessing_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), postprocessing_vs);
	postprocessing_pipeline->add_pass()
	    .add_subpass(vkb::ShaderSource("postprocessing/outline.frag"));

    fxaa_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), postprocessing_vs);
    fxaa_pipeline->add_pass()
            .add_subpass(vkb::ShaderSource("postprocessing/fxaa.frag"));

    cmaa_detect_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), CMAA_vs);
	cmaa_detect_pipeline->add_pass()
			.add_subpass(vkb::ShaderSource("postprocessing/CMAA_Edge_Detect.frag"));

    cmaa_first_intermediary_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), postprocessing_vs);
    cmaa_first_intermediary_pipeline->add_pass<vkb::PostProcessingComputePass>(vkb::ShaderSource("postprocessing/CMAA_Compute_Dispatch1.comp"));

    cmaa_refine_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), CMAA_vs);
    cmaa_refine_pipeline->add_pass<vkb::PostProcessingComputePass>(vkb::ShaderSource("postprocessing/CMAA_Edge_Refine.comp"));

    cmaa_combine_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), CMAA_vs);
    cmaa_combine_pipeline->add_pass<vkb::PostProcessingComputePass>(vkb::ShaderSource("postprocessing/CMAA_Edge_Combine.comp"));

    cmaa_second_intermediary_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), std::move(postprocessing_vs));
	cmaa_second_intermediary_pipeline->add_pass<vkb::PostProcessingComputePass>(vkb::ShaderSource("postprocessing/CMAA_Compute_Dispatch2.comp"));

	cmaa_process_pipeline = std::make_unique<vkb::PostProcessingPipeline>(get_render_context(), std::move(CMAA_vs));
	cmaa_process_pipeline->add_pass<vkb::PostProcessingComputePass>(vkb::ShaderSource("postprocessing/CMAA_Process.comp"));

	update_pipelines();

	stats->request_stats({vkb::StatIndex::frame_times,
	                      vkb::StatIndex::gpu_ext_read_bytes,
	                      vkb::StatIndex::gpu_ext_write_bytes});

	gui = std::make_unique<vkb::Gui>(*this, platform.get_window(), stats.get());

	return true;
}

void CMAASample::prepare_render_context()
{
	get_render_context().prepare(1, std::bind(&CMAASample::create_render_target, this, std::placeholders::_1));
}

void CMAASample::createCMAAResources(vkb::Device &device, const VkExtent3D &extent)
{

    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vkb::core::Image potential_edge_image{device,
                                          VkExtent3D{extent.width/2, extent.height/2, extent.depth},
                                          VK_FORMAT_R8G8B8A8_UNORM,
                                          imageUsage,
                                          VMA_MEMORY_USAGE_GPU_ONLY,
                                          VK_SAMPLE_COUNT_1_BIT};
    vkb::core::Image edge_image{device,
                                VkExtent3D{extent.width/2, extent.height/2, extent.depth},
                                VK_FORMAT_R8_UINT,
                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_GPU_ONLY,
                                VK_SAMPLE_COUNT_1_BIT};
    vkb::core::Image edge_image2{device,
                                 VkExtent3D{extent.width/2, extent.height, extent.depth},
                                 VK_FORMAT_R8_UINT,
                                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 VMA_MEMORY_USAGE_GPU_ONLY,
                                 VK_SAMPLE_COUNT_1_BIT};
    vkb::core::Image CMAA_colourImage{device,
                                      extent,
                                      VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                      VMA_MEMORY_USAGE_GPU_ONLY,
                                      VK_SAMPLE_COUNT_1_BIT};

    std::vector<vkb::core::Image> CMAA_Images;
    CMAA_Images.push_back(std::move(potential_edge_image));
    CMAA_PotentialEdgeRenderTarget = std::make_unique<vkb::RenderTarget>(std::move(CMAA_Images));

    std::vector<vkb::core::Image> CMAA_EdgeImages;
    CMAA_EdgeImages.push_back(std::move(edge_image));
    CMAA_PartialEdgeRenderTarget = std::make_unique<vkb::RenderTarget>(std::move(CMAA_EdgeImages));

    std::vector<vkb::core::Image> CMAA_EdgeImages2;
    CMAA_EdgeImages2.push_back(std::move(edge_image2));
    CMAA_FullEdgeRenderTarget = std::make_unique<vkb::RenderTarget>(std::move(CMAA_EdgeImages2));

    std::vector<vkb::core::Image> CMAA_ColourImages;
    CMAA_ColourImages.push_back(std::move(CMAA_colourImage));
    CMAA_ColourRenderTarget = std::make_unique<vkb::RenderTarget>(std::move(CMAA_ColourImages));

    VkDeviceSize posBufferSize = sizeof(uint32_t) * (extent.width * extent.height / 4);
    VkDeviceSize candidateCountSize = sizeof(uint32_t) * 2;
    VkDeviceSize indirectBufSize = sizeof(VkDispatchIndirectCommand);
    VkDeviceSize wholeBufferSize = posBufferSize*2;
	CMAA_edgeCandidateBuffer = std::make_unique<vkb::core::Buffer>(device, wholeBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                      VMA_MEMORY_USAGE_GPU_ONLY, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	CMAA_edgePosBuffer = std::make_unique<vkb::core::Buffer>(device, wholeBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                      VMA_MEMORY_USAGE_GPU_ONLY, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
	CMAA_edgeCandidateAlloc = std::make_unique<vkb::BufferAllocation>(*CMAA_edgeCandidateBuffer, posBufferSize, 0);
	CMAA_edgePosAlloc = std::make_unique<vkb::BufferAllocation>(*CMAA_edgePosBuffer, posBufferSize, 0);

    CMAA_edgeCountBuffer = std::make_unique<vkb::core::Buffer>(device, candidateCountSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                               VMA_MEMORY_USAGE_GPU_ONLY);
	CMAA_edgeCountAlloc = std::make_unique<vkb::BufferAllocation>(*CMAA_edgeCountBuffer, candidateCountSize, 0);

    CMAA_indirectBuffer = std::make_unique<vkb::core::Buffer>(device, indirectBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                                              VMA_MEMORY_USAGE_GPU_ONLY);
    CMAA_indirectBufAlloc = std::make_unique<vkb::BufferAllocation>(*CMAA_indirectBuffer, indirectBufSize, 0);
}

std::unique_ptr<vkb::RenderTarget> CMAASample::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto &extent = swapchain_image.get_extent();

	auto              depth_format        = vkb::get_suitable_depth_format(device.get_gpu().get_handle());
	bool              msaa_enabled        = sample_count != VK_SAMPLE_COUNT_1_BIT;
	VkImageUsageFlags depth_usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VkImageUsageFlags depth_resolve_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (run_postprocessing)
	{
		// Depth needs to be read by the postprocessing subpass
		if (msaa_enabled && depth_writeback_resolve_supported && resolve_depth_on_writeback)
		{
			// Depth is resolved
			depth_usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
			depth_resolve_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		else
		{
			// Postprocessing reads multisampled depth
			depth_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
			depth_resolve_usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		}
	}
	else
	{
		// Depth attachments are transient
		depth_usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		depth_resolve_usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	}

	vkb::core::Image depth_image{device,
	                             extent,
	                             depth_format,
	                             depth_usage,
	                             VMA_MEMORY_USAGE_GPU_ONLY,
	                             sample_count};

	vkb::core::Image depth_resolve_image{device,
	                                     extent,
	                                     depth_format,
	                                     depth_resolve_usage,
	                                     VMA_MEMORY_USAGE_GPU_ONLY,
	                                     VK_SAMPLE_COUNT_1_BIT};

	VkImageUsageFlags color_ms_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (ColorResolve::OnWriteback == color_resolve_method)
	{
		// Writeback resolve means that the multisampled attachment
		// can be discarded at the end of the render pass
		color_ms_usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	}
	else if (ColorResolve::SeparatePass == color_resolve_method)
	{
		// Multisampled attachment will be stored and
		// resolved outside the render pass
		color_ms_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	vkb::core::Image color_ms_image{device,
	                                extent,
                                    gui_CMAA_enabled ? VK_FORMAT_R8G8B8A8_UNORM : swapchain_image.get_format(),
	                                color_ms_usage,
	                                VMA_MEMORY_USAGE_GPU_ONLY,
	                                sample_count};

    VkImageUsageFlags color_resolve_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	if (run_postprocessing)
	{
		if (ColorResolve::SeparatePass == color_resolve_method)
		{
			// The multisampled color image will be resolved
			// to this attachment with a transfer operation
			color_resolve_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		// The resolved color image will be read by the postprocessing
		// renderpass
		color_resolve_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	vkb::core::Image color_resolve_image{device,
	                                     extent,
                                         gui_CMAA_enabled ? VK_FORMAT_R8G8B8A8_UNORM : swapchain_image.get_format(),
	                                     color_resolve_usage,
	                                     VMA_MEMORY_USAGE_GPU_ONLY,
	                                     VK_SAMPLE_COUNT_1_BIT};

    scene_load_store.clear();
	std::vector<vkb::core::Image> images;

	// Attachment 0 - Swapchain
	// Used by the scene renderpass if postprocessing is disabled
	// Used by the postprocessing renderpass if postprocessing is enabled
	i_swapchain = 0;
	images.push_back(std::move(swapchain_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE});

	// Attachment 1 - Depth
	// Always used by the scene renderpass, may or may not be multisampled
	i_depth = 1;
	images.push_back(std::move(depth_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE});

	// Attachment 2 - Multisampled color
	// Used by the scene renderpass if MSAA is enabled
	i_color_ms = 2;
	images.push_back(std::move(color_ms_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE});

	// Attachment 3 - Resolved color
	// Used as an output by the scene renderpass if MSAA and postprocessing are enabled
	// Used as an input by the postprocessing renderpass
	i_color_resolve = 3;
	images.push_back(std::move(color_resolve_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE});

	// Attachment 4 - Resolved depth
	// Used for writeback depth resolve if MSAA is enabled and the required extension is supported
	i_depth_resolve = 4;
	images.push_back(std::move(depth_resolve_image));
	scene_load_store.push_back({VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE});

	color_atts = {i_swapchain, i_color_ms, i_color_resolve};
	depth_atts = {i_depth, i_depth_resolve};

	createCMAAResources(device, extent);

    return std::make_unique<vkb::RenderTarget>(std::move(images));
}

void CMAASample::update(float delta_time)
{
	if ((gui_run_postprocessing != last_gui_run_postprocessing) ||
	    (gui_sample_count != last_gui_sample_count) ||
	    (gui_color_resolve_method != last_gui_color_resolve_method) ||
	    (gui_resolve_depth_on_writeback != last_gui_resolve_depth_on_writeback) ||
	    (gui_depth_resolve_mode != last_gui_depth_resolve_mode) ||
        (gui_FXAA_enabled != last_gui_FXAA_enabled) ||
        (gui_CMAA_enabled != last_gui_CMAA_enabled))
	{
		run_postprocessing         = gui_run_postprocessing;
		sample_count               = gui_sample_count;
		color_resolve_method       = gui_color_resolve_method;
		resolve_depth_on_writeback = gui_resolve_depth_on_writeback;
		depth_resolve_mode         = gui_depth_resolve_mode;

		update_pipelines();

		last_gui_run_postprocessing         = gui_run_postprocessing;
		last_gui_sample_count               = gui_sample_count;
		last_gui_color_resolve_method       = gui_color_resolve_method;
		last_gui_resolve_depth_on_writeback = gui_resolve_depth_on_writeback;
		last_gui_depth_resolve_mode         = gui_depth_resolve_mode;
        last_gui_FXAA_enabled               = gui_FXAA_enabled;
        last_gui_CMAA_enabled               = gui_CMAA_enabled;
    }

	VulkanSample::update(delta_time);
}

void CMAASample::update_pipelines()
{
	bool msaa_enabled = sample_count != VK_SAMPLE_COUNT_1_BIT;
	if (run_postprocessing)
	{
		update_for_scene_and_postprocessing(msaa_enabled);
	}
	else
	{
		update_for_scene_only(msaa_enabled);
	}

	// Default swapchain usage flags
	std::set<VkImageUsageFlagBits> swapchain_usage = {VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
	if (ColorResolve::SeparatePass == color_resolve_method && !run_postprocessing)
	{
		// The multisampled color image will be resolved
		// to the swapchain with a transfer operation
		swapchain_usage.insert(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	}

	get_device().wait_idle();

	get_render_context().update_swapchain(swapchain_usage);
}

void CMAASample::update_for_scene_only(bool msaa_enabled)
{
	auto &scene_subpass = scene_pipeline->get_active_subpass();
	scene_subpass->set_sample_count(sample_count);

	if (msaa_enabled)
	{
		// Render multisampled color, to be resolved to the swapchain
		use_multisampled_color(scene_subpass, scene_load_store, i_swapchain);
	}
	else
	{
		// Render color to the swapchain
		use_singlesampled_color(scene_subpass, scene_load_store, i_swapchain);
	}

	// Depth attachment is transient, it will not be needed after the renderpass
	// If it is multisampled, there is no need to resolve it
	scene_load_store[i_depth].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	disable_depth_writeback_resolve(scene_subpass, scene_load_store);

	// Auxiliary single-sampled color attachment is not used
	scene_load_store[i_color_resolve].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// Update the scene renderpass
	scene_pipeline->set_load_store(scene_load_store);
}

void CMAASample::update_for_scene_and_postprocessing(bool msaa_enabled)
{
	auto &scene_subpass = scene_pipeline->get_active_subpass();
	scene_subpass->set_sample_count(sample_count);

	// The color and depth attachments will be the input of the postprocessing renderpass
	if (msaa_enabled)
	{
		// Resolve multisampled color to an intermediate attachment
		use_multisampled_color(scene_subpass, scene_load_store, i_color_resolve);

		// Store multisampled depth
		// Resolve it first if enabled and supported,
		store_multisampled_depth(scene_subpass, scene_load_store);
	}
	else
	{
		// Render color to an intermediate attachment
		use_singlesampled_color(scene_subpass, scene_load_store, i_color_resolve);

		// Store single-sampled depth
		scene_load_store[i_depth].store_op = gui_FXAA_enabled || gui_CMAA_enabled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		disable_depth_writeback_resolve(scene_subpass, scene_load_store);
	}

	// Swapchain is not used in the scene renderpass
	scene_load_store[i_swapchain].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// Update the scene renderpass
	scene_pipeline->set_load_store(scene_load_store);
}

void CMAASample::use_multisampled_color(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store, uint32_t resolve_attachment)
{
	// Render to multisampled color attachment
	subpass->set_output_attachments({i_color_ms});

	// Resolve color
	if (ColorResolve::OnWriteback == color_resolve_method)
	{
		// Good practice
		// Multisampled attachment is transient
		// This allows tilers to completely avoid writing out the multisampled attachment to memory,
		// a considerable performance and bandwidth improvement
		load_store[i_color_ms].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		// Enable writeback resolve to single-sampled attachment
		subpass->set_color_resolve_attachments({resolve_attachment});

		// Save resolved attachment
		load_store[resolve_attachment].store_op = VK_ATTACHMENT_STORE_OP_STORE;
	}
	else if (ColorResolve::SeparatePass == color_resolve_method)
	{
		// Bad practice
		// Save multisampled color attachment, will be resolved outside the renderpass
		// Storing multisampled color should be avoided
		load_store[i_color_ms].store_op = VK_ATTACHMENT_STORE_OP_STORE;

		// Disable writeback resolve
		subpass->set_color_resolve_attachments({});
		load_store[resolve_attachment].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
}

void CMAASample::use_singlesampled_color(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store, uint32_t output_attachment)
{
	// Render to a single-sampled attachment
	subpass->set_output_attachments({output_attachment});
	load_store[output_attachment].store_op = VK_ATTACHMENT_STORE_OP_STORE;

	// Multisampled color attachment is not used
	load_store[i_color_ms].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// Disable writeback resolve
	subpass->set_color_resolve_attachments({});
}

void CMAASample::store_multisampled_depth(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store)
{
	if (depth_writeback_resolve_supported && resolve_depth_on_writeback)
	{
		// Good practice
		// Multisampled attachment is transient
		// This allows tilers to completely avoid writing out the multisampled attachment to memory,
		// a considerable performance and bandwidth improvement
		load_store[i_depth].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		// Enable writeback resolve to single-sampled attachment
		subpass->set_depth_stencil_resolve_attachment(i_depth_resolve);
		subpass->set_depth_stencil_resolve_mode(depth_resolve_mode);

		// Save resolved attachment
		load_store[i_depth_resolve].store_op = VK_ATTACHMENT_STORE_OP_STORE;
	}
	else
	{
		// Bad practice
		// Save multisampled depth attachment, which cannot be resolved outside the renderpass
		// Storing multisampled depth should be avoided
		load_store[i_depth].store_op = VK_ATTACHMENT_STORE_OP_STORE;

		// Disable writeback resolve
		disable_depth_writeback_resolve(subpass, load_store);
	}
}

void CMAASample::disable_depth_writeback_resolve(std::unique_ptr<vkb::Subpass> &subpass, std::vector<vkb::LoadStoreInfo> &load_store)
{
	// Auxiliary single-sampled depth attachment is not used
	load_store[i_depth_resolve].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// Disable writeback resolve
	subpass->set_depth_stencil_resolve_attachment(VK_ATTACHMENT_UNUSED);
	subpass->set_depth_stencil_resolve_mode(VK_RESOLVE_MODE_NONE);
}

void CMAASample::draw(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target)
{
	auto &views = render_target.get_views();
	auto swapchain_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		memory_barrier.new_layout      = swapchain_layout;
		memory_barrier.src_access_mask = 0;
		memory_barrier.dst_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		for (auto &i_color : color_atts)
		{
			command_buffer.image_memory_barrier(views.at(i_color), memory_barrier);
			render_target.set_layout(i_color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}
	}

	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
		memory_barrier.new_layout      = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		memory_barrier.src_access_mask = 0;
		memory_barrier.dst_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		for (auto &i_depth : depth_atts)
		{
			command_buffer.image_memory_barrier(views.at(i_depth), memory_barrier);
			render_target.set_layout(i_depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}
	}

	auto &extent = render_target.get_extent();

	VkViewport viewport{};
	viewport.width    = static_cast<float>(extent.width);
	viewport.height   = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	command_buffer.set_viewport(0, {viewport});

	VkRect2D scissor{};
	scissor.extent = extent;
	command_buffer.set_scissor(0, {scissor});

	scene_pipeline->draw(command_buffer, render_target);

	if (!run_postprocessing)
	{
		// If postprocessing is enabled the GUI will be drawn
		// at the end of the postprocessing renderpass
		if (gui)
		{
			gui->draw(command_buffer);
		}
	}

	command_buffer.end_render_pass();

	bool msaa_enabled = sample_count != VK_SAMPLE_COUNT_1_BIT;

	if (msaa_enabled && ColorResolve::SeparatePass == color_resolve_method)
	{
		if (run_postprocessing)
		{
			resolve_color_separate_pass(command_buffer, views, i_color_resolve, swapchain_layout);
		}
		else
		{
			resolve_color_separate_pass(command_buffer, views, i_swapchain, swapchain_layout);
		}
	}

	if (run_postprocessing)
	{
		// Run a second renderpass
		postprocessing(command_buffer, render_target, swapchain_layout, msaa_enabled);
		command_buffer.set_viewport(0, {viewport});
	}

	{
		// Prepare swapchain for presentation
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = swapchain_layout;
		memory_barrier.new_layout      = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		memory_barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		memory_barrier.src_stage_mask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memory_barrier.dst_stage_mask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		command_buffer.image_memory_barrier(views.at(i_swapchain), memory_barrier);
	}
}

void CMAASample::clearCMAAImages(vkb::CommandBuffer &command_buffer)
{
    vkb::ImageMemoryBarrier edgeBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT ,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    command_buffer.image_memory_barrier(CMAA_PartialEdgeRenderTarget->get_views()[0], edgeBarrier);
    vkb::ImageMemoryBarrier finalEdgeBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    command_buffer.image_memory_barrier(CMAA_FullEdgeRenderTarget->get_views()[0], finalEdgeBarrier);

    VkClearColorValue clearColour = {0, 0, 0, 0};
    command_buffer.clear_image(CMAA_PartialEdgeRenderTarget->get_views()[0].get_image(), clearColour);
    command_buffer.clear_image(CMAA_FullEdgeRenderTarget->get_views()[0].get_image(), clearColour);

    edgeBarrier = {VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL};
    command_buffer.image_memory_barrier(CMAA_PartialEdgeRenderTarget->get_views()[0], edgeBarrier);
    CMAA_PartialEdgeRenderTarget->set_layout(0, VK_IMAGE_LAYOUT_GENERAL);
    finalEdgeBarrier = {VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL};
    command_buffer.image_memory_barrier(CMAA_FullEdgeRenderTarget->get_views()[0], finalEdgeBarrier);
    CMAA_FullEdgeRenderTarget->set_layout(0, VK_IMAGE_LAYOUT_GENERAL);
}

void CMAASample::postprocessing(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target,
                                VkImageLayout &swapchain_layout, bool msaa_enabled)
{
	auto        depth_attachment   = (msaa_enabled && depth_writeback_resolve_supported && resolve_depth_on_writeback) ? i_depth_resolve : i_depth;
	bool        multisampled_depth = msaa_enabled && !(depth_writeback_resolve_supported && resolve_depth_on_writeback);
	std::string depth_sampler_name = multisampled_depth ? "ms_depth_sampler" : "depth_sampler";

	glm::vec4 near_far = {camera->get_far_plane(), camera->get_near_plane(), -1.0f, -1.0f};

	if (gui_FXAA_enabled){
        glm::vec2 invScreen = 1.f / glm::vec2(render_target.get_extent().width, render_target.get_extent().height);
        auto &fxaa_pass = fxaa_pipeline->get_pass(0);
        fxaa_pass.set_uniform_data(invScreen);

		auto &fxaa_subpass = fxaa_pass.get_subpass(0);
        fxaa_subpass.get_fs_variant().clear();
        fxaa_subpass.get_fs_variant().add_define("FXAA_ON");
        fxaa_subpass
				.bind_sampled_image("samplerTexture", i_color_resolve);

		// Second render pass
		// NOTE: Color and depth attachments are automatically transitioned to be bound as textures
        fxaa_pipeline->draw(command_buffer, render_target);
	} else if(gui_CMAA_enabled) {

        clearCMAAImages(command_buffer);

        vkb::BufferMemoryBarrier countBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);
        vkb::BufferMemoryBarrier candidatePosBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT};
        command_buffer.buffer_memory_barrier(CMAA_edgeCandidateAlloc->get_buffer(), CMAA_edgeCandidateAlloc->get_offset(), CMAA_edgeCandidateAlloc->get_size(), candidatePosBarrier);

		/// First CMAA Pass
		auto &cmaa_subpass = cmaa_detect_pipeline->get_pass(0).get_subpass(0);
        cmaa_subpass
                .bind_sampled_image("inputSceneTexture", vkb::core::SampledImage(i_color_resolve, &render_target))
                .bind_storage_image("outputSceneImage", vkb::core::SampledImage(0, CMAA_ColourRenderTarget.get()))
                .bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
				.bind_storage_buffer("candidatePosBuffer", *CMAA_edgeCandidateAlloc);
        cmaa_detect_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);
		command_buffer.end_render_pass();

        countBarrier = {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);
        vkb::BufferMemoryBarrier indirectBarrier = {VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT};
        command_buffer.buffer_memory_barrier(CMAA_indirectBufAlloc->get_buffer(), CMAA_indirectBufAlloc->get_offset(), CMAA_indirectBufAlloc->get_size(), indirectBarrier);

		cmaa_first_intermediary_pipeline->get_pass<vkb::PostProcessingComputePass>(0)
		        .bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
				.bind_storage_buffer("indirectBuffer", *CMAA_indirectBufAlloc);
        cmaa_first_intermediary_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);

        indirectBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
        command_buffer.buffer_memory_barrier(CMAA_indirectBufAlloc->get_buffer(), CMAA_indirectBufAlloc->get_offset(), CMAA_indirectBufAlloc->get_size(), indirectBarrier);
        countBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);
		candidatePosBarrier = {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgeCandidateAlloc->get_buffer(), CMAA_edgeCandidateAlloc->get_offset(), CMAA_edgeCandidateAlloc->get_size(), candidatePosBarrier);

		/// Second CMAA Pass
        cmaa_refine_pipeline->get_pass<vkb::PostProcessingComputePass>(0)
                .set_dispatch_size(CMAA_indirectBufAlloc.get())
				.bind_sampled_image("candidateTexture", vkb::core::SampledImage(0, CMAA_PotentialEdgeRenderTarget.get()))
				.bind_storage_image("partialEdgeImage", vkb::core::SampledImage(0, CMAA_PartialEdgeRenderTarget.get()))
				.bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
				.bind_storage_buffer("candidatePosBuffer", *CMAA_edgeCandidateAlloc);
		cmaa_refine_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);

		indirectBarrier = {VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_indirectBufAlloc->get_buffer(), CMAA_indirectBufAlloc->get_offset(), CMAA_indirectBufAlloc->get_size(), indirectBarrier);
		countBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);

        cmaa_first_intermediary_pipeline->get_pass<vkb::PostProcessingComputePass>(0)
                .bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
                .bind_storage_buffer("indirectBuffer", *CMAA_indirectBufAlloc);
        cmaa_first_intermediary_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);

        indirectBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
		command_buffer.buffer_memory_barrier(CMAA_indirectBufAlloc->get_buffer(), CMAA_indirectBufAlloc->get_offset(), CMAA_indirectBufAlloc->get_size(), indirectBarrier);
		countBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);
		candidatePosBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgeCandidateAlloc->get_buffer(), CMAA_edgeCandidateAlloc->get_offset(), CMAA_edgeCandidateAlloc->get_size(), candidatePosBarrier);
		vkb::BufferMemoryBarrier edgePosBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgePosAlloc->get_buffer(), CMAA_edgePosAlloc->get_offset(), CMAA_edgePosAlloc->get_size(), edgePosBarrier);

		/// Third CMAA Pass
		cmaa_combine_pipeline->get_pass<vkb::PostProcessingComputePass>(0)
                .set_dispatch_size(CMAA_indirectBufAlloc.get())
                .bind_sampled_image("partialEdgeTexture", vkb::core::SampledImage(0, CMAA_PartialEdgeRenderTarget.get()))
				.bind_sampled_image("inputSceneTexture", vkb::core::SampledImage(i_color_resolve, &render_target))
                .bind_storage_image("fullEdgeImage", vkb::core::SampledImage(0, CMAA_FullEdgeRenderTarget.get()))
				.bind_storage_image("outputSceneImage", vkb::core::SampledImage(0, CMAA_ColourRenderTarget.get()))
				.bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
                .bind_storage_buffer("candidatePosBuffer", *CMAA_edgeCandidateAlloc)
                .bind_storage_buffer("edgePosBuffer", *CMAA_edgePosAlloc);
        cmaa_combine_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);

		indirectBarrier = {VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_indirectBufAlloc->get_buffer(), CMAA_indirectBufAlloc->get_offset(), CMAA_indirectBufAlloc->get_size(), indirectBarrier);
		countBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);

		cmaa_second_intermediary_pipeline->get_pass<vkb::PostProcessingComputePass>(0)
				.bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
				.bind_storage_buffer("indirectBuffer", *CMAA_indirectBufAlloc);
		cmaa_second_intermediary_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);

		indirectBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT};
		command_buffer.buffer_memory_barrier(CMAA_indirectBufAlloc->get_buffer(), CMAA_indirectBufAlloc->get_offset(), CMAA_indirectBufAlloc->get_size(), indirectBarrier);
		countBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgeCountAlloc->get_buffer(), CMAA_edgeCountAlloc->get_offset(), CMAA_edgeCountAlloc->get_size(), countBarrier);
		edgePosBarrier = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT};
		command_buffer.buffer_memory_barrier(CMAA_edgePosAlloc->get_buffer(), CMAA_edgePosAlloc->get_offset(), CMAA_edgePosAlloc->get_size(), edgePosBarrier);

		/// Fourth CMAA Pass
		glm::vec2 invScreen = 1.f / glm::vec2(render_target.get_extent().width, render_target.get_extent().height);
		cmaa_process_pipeline->get_pass<vkb::PostProcessingComputePass>(0)
				.set_dispatch_size(CMAA_indirectBufAlloc.get())
				.bind_sampled_image("fullEdgeTexture", vkb::core::SampledImage(0, CMAA_FullEdgeRenderTarget.get()))
				.bind_sampled_image("inputSceneTexture", vkb::core::SampledImage(i_color_resolve, &render_target))
				.bind_storage_image("outputSceneImage", vkb::core::SampledImage(0, CMAA_ColourRenderTarget.get()))
				.bind_storage_buffer("threadCountBuffer", *CMAA_edgeCountAlloc)
				.bind_storage_buffer("edgePosBuffer", *CMAA_edgePosAlloc)
				.set_uniform_data(invScreen);
		cmaa_process_pipeline->draw(command_buffer, *CMAA_PotentialEdgeRenderTarget);

        auto &fxaa_pass = fxaa_pipeline->get_pass(0);
        fxaa_pass.set_uniform_data(invScreen);

        auto &fxaa_subpass = fxaa_pass.get_subpass(0);
        fxaa_subpass.get_fs_variant().clear();
        fxaa_subpass
                .bind_sampled_image("samplerTexture", vkb::core::SampledImage(0, CMAA_ColourRenderTarget.get()));

        // Second render pass
        // NOTE: Color and depth attachments are automatically transitioned to be bound as textures
        fxaa_pipeline->draw(command_buffer, render_target);
    } else {
		auto &postprocessing_pass = postprocessing_pipeline->get_pass(0);
		postprocessing_pass.set_uniform_data(near_far);

		auto &postprocessing_subpass = postprocessing_pass.get_subpass(0);
		postprocessing_subpass.get_fs_variant().clear();
		if (multisampled_depth) {
			postprocessing_subpass.get_fs_variant().add_define("MS_DEPTH");
		}
		postprocessing_subpass
				.bind_sampled_image(depth_sampler_name, depth_attachment)
				.bind_sampled_image("color_sampler", i_color_resolve);

		// Second render pass
		// NOTE: Color and depth attachments are automatically transitioned to be bound as textures
		postprocessing_pipeline->draw(command_buffer, render_target);
	}

	if (gui)
	{
		gui->draw(command_buffer);
	}

	command_buffer.end_render_pass();
}

void CMAASample::resolve_color_separate_pass(vkb::CommandBuffer &command_buffer, const std::vector<vkb::core::ImageView> &views,
                                             uint32_t color_destination, VkImageLayout &color_layout)
{
	{
		// The multisampled color is the source of the resolve operation
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		memory_barrier.new_layout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

		command_buffer.image_memory_barrier(views.at(i_color_ms), memory_barrier);
	}

	VkImageSubresourceLayers subresource = {0};
	subresource.aspectMask               = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource.layerCount               = 1;

	VkImageResolve image_resolve = {0};
	image_resolve.srcSubresource = subresource;
	image_resolve.dstSubresource = subresource;
	image_resolve.extent         = VkExtent3D{get_render_context().get_surface_extent().width, get_render_context().get_surface_extent().height, 1};

	{
		// Prepare destination image for transfer operation
		auto color_new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout     = color_layout;
		memory_barrier.new_layout     = color_new_layout;
		memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

		color_layout = color_new_layout;

		command_buffer.image_memory_barrier(views.at(color_destination), memory_barrier);
	}

	// Resolve multisampled attachment to destination, extremely expensive
	command_buffer.resolve_image(views.at(i_color_ms).get_image(), views.at(color_destination).get_image(), {image_resolve});

	// Transition attachments out of transfer stage
	{
		auto color_new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout     = color_layout;
		memory_barrier.new_layout     = color_new_layout;
		memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		color_layout = color_new_layout;

		command_buffer.image_memory_barrier(views.at(color_destination), memory_barrier);
	}

	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		memory_barrier.new_layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		memory_barrier.src_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		memory_barrier.dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		command_buffer.image_memory_barrier(views.at(i_color_ms), memory_barrier);
	}
}

void CMAASample::prepare_supported_sample_count_list()
{
	VkPhysicalDeviceProperties gpu_properties;
	vkGetPhysicalDeviceProperties(get_device().get_gpu().get_handle(), &gpu_properties);

	VkSampleCountFlags supported_by_depth_and_color = gpu_properties.limits.framebufferColorSampleCounts & gpu_properties.limits.framebufferDepthSampleCounts;

	// All possible sample counts are listed here from most to least preferred as default
	// On Mali GPUs 4X MSAA is recommended as best performance/quality trade-off
	std::vector<VkSampleCountFlagBits> counts = {VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_8_BIT,
	                                             VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_64_BIT,
	                                             VK_SAMPLE_COUNT_1_BIT};

	for (auto &count : counts)
	{
		if (supported_by_depth_and_color & count)
		{
			supported_sample_count_list.push_back(count);

			if (sample_count == VK_SAMPLE_COUNT_1_BIT)
			{
				// Set default sample count based on the priority defined above
				sample_count          = count;
				gui_sample_count      = count;
				last_gui_sample_count = count;
			}
		}
	}
}

void CMAASample::prepare_depth_resolve_mode_list()
{
	if (instance->is_enabled(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
	{
		VkPhysicalDeviceProperties2KHR gpu_properties{};
		gpu_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
		VkPhysicalDeviceDepthStencilResolvePropertiesKHR depth_resolve_properties{};
		depth_resolve_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES_KHR;
		gpu_properties.pNext           = static_cast<void *>(&depth_resolve_properties);
		vkGetPhysicalDeviceProperties2KHR(get_device().get_gpu().get_handle(), &gpu_properties);

		if (depth_resolve_properties.supportedDepthResolveModes == 0)
		{
			LOGW("No depth stencil resolve modes supported");
			depth_writeback_resolve_supported = false;
		}
		else
		{
			// All possible modes are listed here from most to least preferred as default
			std::vector<VkResolveModeFlagBits> modes = {VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, VK_RESOLVE_MODE_MIN_BIT,
			                                            VK_RESOLVE_MODE_MAX_BIT, VK_RESOLVE_MODE_AVERAGE_BIT};

			for (auto &mode : modes)
			{
				if (depth_resolve_properties.supportedDepthResolveModes & mode)
				{
					supported_depth_resolve_mode_list.push_back(mode);

					if (depth_resolve_mode == VK_RESOLVE_MODE_NONE)
					{
						// Set default mode based on the priority defined above
						depth_resolve_mode          = mode;
						gui_depth_resolve_mode      = mode;
						last_gui_depth_resolve_mode = mode;
					}
				}
			}
		}
	}
}

void CMAASample::draw_gui()
{
	auto       msaa_enabled = sample_count != VK_SAMPLE_COUNT_1_BIT;
	const bool landscape    = camera->get_aspect_ratio() > 1.0f;
	uint32_t   lines        = landscape ? 3 : 4;

	gui->show_options_window(
	    [this, msaa_enabled, landscape]() {
		    ImGui::AlignTextToFramePadding();
		    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.4f);
			std::string previewtext;
		    if (gui_FXAA_enabled)
                previewtext = "FXAA";
            else if (gui_CMAA_enabled)
                previewtext = "CMAA";
            else
                previewtext = to_string(gui_sample_count);
		    if (ImGui::BeginCombo("##sample_count", previewtext.c_str()))
		    {
                bool is_selected;

                for (int n = 0; n < supported_sample_count_list.size(); n++)
				{
                    is_selected = (gui_sample_count == supported_sample_count_list[n] && !gui_FXAA_enabled && !gui_CMAA_enabled);
					if (ImGui::Selectable(to_string(supported_sample_count_list[n]).c_str(), is_selected)) {
                        gui_sample_count = supported_sample_count_list[n];
                        gui_FXAA_enabled = false;
                        gui_CMAA_enabled = false;
                        gui_run_postprocessing = false;
                    }
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
                is_selected = gui_FXAA_enabled;
                if (ImGui::Selectable("FXAA", is_selected))
                {
                    gui_FXAA_enabled = true;
                    gui_sample_count = VK_SAMPLE_COUNT_1_BIT;
                    gui_CMAA_enabled = false;
                    gui_run_postprocessing = true;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();

                is_selected = gui_CMAA_enabled;
                if (ImGui::Selectable("CMAA", is_selected))
                {
                    gui_CMAA_enabled = true;
                    gui_sample_count = VK_SAMPLE_COUNT_1_BIT;
                    gui_FXAA_enabled = false;
                    gui_run_postprocessing = true;
                }
                if (is_selected)
                  ImGui::SetItemDefaultFocus();

                ImGui::EndCombo();
		    }
		    if (landscape)
		    {
			    ImGui::SameLine();
		    }
		    if (!gui_FXAA_enabled && !gui_CMAA_enabled)
		        ImGui::Checkbox("Post-processing (2 renderpasses)", &gui_run_postprocessing);

            ImGui::Text("Resolve color: ");
		    ImGui::SameLine();
		    if (msaa_enabled)
		    {
			    ImGui::RadioButton("On writeback", &gui_color_resolve_method, ColorResolve::OnWriteback);
			    ImGui::SameLine();
			    ImGui::RadioButton("Separate", &gui_color_resolve_method, ColorResolve::SeparatePass);
		    }
		    else
		    {
			    ImGui::Text("n/a");
		    }

		    ImGui::Text("Resolve depth: ");
		    ImGui::SameLine();
		    if (msaa_enabled && run_postprocessing)
		    {
			    if (depth_writeback_resolve_supported)
			    {
				    ImGui::Checkbox("##resolve_depth", &gui_resolve_depth_on_writeback);
				    ImGui::SameLine();
				    ImGui::Text("On writeback");
				    ImGui::SameLine();
				    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
				    if (ImGui::BeginCombo("##resolve_mode", to_string(gui_depth_resolve_mode).c_str()))
				    {
					    for (int n = 0; n < supported_depth_resolve_mode_list.size(); n++)
					    {
						    bool is_selected = (gui_depth_resolve_mode == supported_depth_resolve_mode_list[n]);
						    if (ImGui::Selectable(to_string(supported_depth_resolve_mode_list[n]).c_str(), is_selected))
							    gui_depth_resolve_mode = supported_depth_resolve_mode_list[n];
						    if (is_selected)
						    {
							    ImGui::SetItemDefaultFocus();
						    }
					    }
					    ImGui::EndCombo();
				    }
			    }
			    else
			    {
				    ImGui::Text("Not supported");
			    }
		    }
		    else
		    {
			    ImGui::Text("n/a");
		    }
	    },
	    lines);
}

std::unique_ptr<vkb::VulkanSample> create_cmaa()
{
	return std::make_unique<CMAASample>();
}
