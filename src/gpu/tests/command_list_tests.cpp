#include "gpu/command_list.h"

#include "catch.hpp"


namespace
{
}

TEST_CASE("commandlist-tests-alloc")
{
	Core::HandleAllocator handleAllocator = Core::HandleAllocator(GPU::ResourceType::MAX);
	GPU::CommandList commandList(handleAllocator, sizeof(size_t) * 4);

	// Allocate 4 blocks of data.
	REQUIRE(commandList.Alloc(sizeof(size_t)) != nullptr);
	REQUIRE(commandList.Alloc(sizeof(size_t)) != nullptr);
	REQUIRE(commandList.Alloc(sizeof(size_t)) != nullptr);
	REQUIRE(commandList.Alloc(sizeof(size_t)) != nullptr);
	// Should fail here.
	REQUIRE(commandList.Alloc(sizeof(size_t)) == nullptr);
}

TEST_CASE("commandlist-tests-commands")
{
	Core::HandleAllocator handleAllocator = Core::HandleAllocator(GPU::ResourceType::MAX);

	// Allocate a bunch of dummy handles.
	GPU::Handle buffer0Handle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::BUFFER);
	GPU::Handle buffer1Handle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::BUFFER);
	GPU::Handle texture0Handle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::TEXTURE);
	GPU::Handle texture1Handle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::TEXTURE);
	GPU::Handle pipelineBindingHandle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::PIPELINE_BINDING_SET);
	GPU::Handle drawBindingHandle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::DRAW_BINDING_SET);
	GPU::Handle frameBindingHandle = handleAllocator.Alloc<GPU::Handle>(GPU::ResourceType::FRAME_BINDING_SET);

	GPU::CommandList commandList(handleAllocator);
	float f[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	u32 u[4] = {0, 0, 0, 0};
	GPU::TextureSubResourceData texSubRsc;
	texSubRsc.data_ = u;
	texSubRsc.rowPitch_ = 4;
	texSubRsc.slicePitch_ = 16;
	GPU::Box box;
	box.x_ = 0;
	box.y_ = 0;
	box.z_ = 0;
	box.w_ = 1;
	box.h_ = 1;
	box.d_ = 1;
	GPU::Point point;
	point.x_ = 0;
	point.y_ = 0;
	point.z_ = 0;
	GPU::BindingDSV dsv;
	dsv.resource_ = texture0Handle;
	dsv.format_ = GPU::Format::D24_UNORM_S8_UINT;
	dsv.dimension_ = GPU::ViewDimension::TEX2D;

	// Draws.
	REQUIRE(commandList.Draw(
	    pipelineBindingHandle, drawBindingHandle, frameBindingHandle, GPU::PrimitiveTopology::TRIANGLE_LIST, 0, 0, 3, 0, 1));
	REQUIRE(commandList.DrawIndirect(pipelineBindingHandle, drawBindingHandle, frameBindingHandle, buffer0Handle, 0));
	// Dispatches.
	REQUIRE(commandList.Dispatch(pipelineBindingHandle, 1, 1, 1));
	REQUIRE(commandList.DispatchIndirect(pipelineBindingHandle, buffer0Handle, 0));
	// Clears.
	REQUIRE(commandList.ClearRTV(frameBindingHandle, 0, f));
	REQUIRE(commandList.ClearDSV(frameBindingHandle, 0.0f, 0));
	REQUIRE(commandList.ClearUAV(pipelineBindingHandle, 0, f));
	REQUIRE(commandList.ClearUAV(pipelineBindingHandle, 0, u));
	// Updates.
	REQUIRE(commandList.UpdateBuffer(buffer0Handle, 0, sizeof(u), u));
	REQUIRE(commandList.UpdateTextureSubResource(texture0Handle, 0, texSubRsc));
	// Copies.
	REQUIRE(commandList.CopyBuffer(buffer0Handle, 0, sizeof(u), buffer1Handle, 0));
	REQUIRE(commandList.CopyTextureSubResource(texture0Handle, 0, box, texture1Handle, 0, point));
	REQUIRE(commandList.CopyTextureSubResource(texture0Handle, 0, box, texture0Handle, 1, point));

	// Check we have valid commands.
	for(auto* command : commandList)
	{
		switch(command->type_)
		{
		case GPU::CommandType::DRAW:
		case GPU::CommandType::DRAW_INDIRECT:
		case GPU::CommandType::DISPATCH:
		case GPU::CommandType::DISPATCH_INDIRECT:
		case GPU::CommandType::CLEAR_RTV:
		case GPU::CommandType::CLEAR_DSV:
		case GPU::CommandType::CLEAR_UAV:
		case GPU::CommandType::UPDATE_BUFFER:
		case GPU::CommandType::UPDATE_TEXTURE_SUBRESOURCE:
		case GPU::CommandType::COPY_BUFFER:
		case GPU::CommandType::COPY_TEXTURE_SUBRESOURCE:
			break;
		default:
			FAIL("Invalid command type.");
		}
	}
}
