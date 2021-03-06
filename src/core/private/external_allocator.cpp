#include "core/external_allocator.h"
#include "core/misc.h"

#if !CODE_INLINE
#include "core/private/external_allocator.inl"
#endif

#include "core/debug.h"
#include "etlsf.h"

#include <utility>

namespace Core
{
	ExternalAllocator::ExternalAllocator(i32 size, i32 maxAllocations)
	{
		DBG_ASSERT(size > 0);
		DBG_ASSERT(maxAllocations > 0 && maxAllocations <= 0xffff)

		size = Core::PotRoundDown(size, SIZE_ALIGNMENT);
		arena_ = etlsf_create((u32)size, (u16)maxAllocations);
	}

	ExternalAllocator::~ExternalAllocator() { etlsf_destroy(arena_); }

	ExternalAllocator::ExternalAllocator(ExternalAllocator&& other) { std::swap(arena_, other.arena_); }

	ExternalAllocator& ExternalAllocator::operator=(ExternalAllocator&& other)
	{
		std::swap(arena_, other.arena_);
		return *this;
	}

	u16 ExternalAllocator::AllocRange(i32 size)
	{
		DBG_ASSERT(size > 0);
		return etlsf_alloc_range(arena_, size).value;
	}

	void ExternalAllocator::FreeRange(u16 id)
	{
		const etlsf_alloc_t alloc = {id};
		etlsf_free_range(arena_, alloc);
	}

	ExternalAlloc ExternalAllocator::GetAlloc(u16 id) const
	{
		const etlsf_alloc_t alloc = {id};
		ExternalAlloc retVal;
		retVal.offset_ = (i32)etlsf_alloc_offset(arena_, alloc);
		retVal.size_ = (i32)etlsf_alloc_size(arena_, alloc);
		if(retVal.offset_ == 0 && retVal.size_ == 0)
		{
			retVal.offset_ = -1;
			retVal.size_ = -1;
		}
		return retVal;
	}
}
