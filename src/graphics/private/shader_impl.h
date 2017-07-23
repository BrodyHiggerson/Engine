#pragma once
#include "core/string.h"
#include "core/vector.h"
#include "gpu/resources.h"
#include "graphics/shader.h"

namespace Graphics
{
	static const i32 MAX_NAME_LENGTH = 64;

	struct ShaderHeader
	{
		/// Magic number.
		static const u32 MAGIC = 0x229C08ED;
		/// Major version signifies a breaking change to the binary format.
		static const i16 MAJOR_VERSION = 0x0000;
		/// Mimor version signifies non-breaking change to binary format.
		static const i16 MINOR_VERSION = 0x0002;

		u32 magic_ = MAGIC;
		i16 majorVersion_ = MAJOR_VERSION;
		i16 minorVersion_ = MINOR_VERSION;

		i32 numCBuffers_ = 0;
		i32 numSamplers_ = 0;
		i32 numSRVs_ = 0;
		i32 numUAVs_ = 0;
		i32 numShaders_ = 0;
		i32 numTechniques_ = 0;
	};

	struct ShaderBindingHeader
	{
		char name_[MAX_NAME_LENGTH];
	};

	struct ShaderBytecodeHeader
	{
		i32 numCBuffers_ = 0;
		i32 numSamplers_ = 0;
		i32 numSRVs_ = 0;
		i32 numUAVs_ = 0;
		GPU::ShaderType type_ = GPU::ShaderType::INVALID;
		i32 offset_ = 0;
		i32 numBytes_ = 0;
	};

	struct ShaderBindingMapping
	{
		i32 binding_ = 0;
		i32 dstSlot_ = 0;
	};

	struct ShaderTechniqueHeader
	{
		char name_[MAX_NAME_LENGTH];
		i32 vs_;
		i32 gs_;
		i32 hs_;
		i32 ds_;
		i32 ps_;
		i32 cs_;
		GPU::RenderState rs_; // TODO: Store separately.
	};

	struct ShaderImpl
	{
		Core::String name_;
		Graphics::ShaderHeader header_;
		Core::Vector<ShaderBindingHeader> bindingHeaders_;
		Core::Vector<ShaderBytecodeHeader> bytecodeHeaders_;
		Core::Vector<ShaderBindingMapping> bindingMappings_;
		Core::Vector<ShaderTechniqueHeader> techniqueHeaders_;
		Core::Vector<u8> bytecode_;

		Core::Vector<GPU::Handle> shaders_;

		// Technique data.
		Core::Vector<u32> techniqueDescHashes_;
		Core::Vector<ShaderTechniqueDesc> techniqueDescs_; //
		Core::Vector<GPU::Handle> pipelineStates_;
		i32 liveTechniques_ = 0;
	};

	struct ShaderTechniqueImpl
	{
		ShaderImpl* shader_ = nullptr;
		const ShaderTechniqueHeader* header_ = nullptr;

		bool bsDirty_ = true;
		GPU::PipelineBindingSetDesc bs_;
		GPU::Handle bsHandle_;
	};

} // namespace Graphics