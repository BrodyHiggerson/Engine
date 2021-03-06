#pragma once

#include "image/color.h"

namespace Image
{
	class Image;

	/**
	 * Useful constants for quality checking with PSNR.
	 */
	static const f32 INFINITE_PSNR = 99999.0f;
	static const RGBAColor INFINITE_PSNR_RGBA = RGBAColor(INFINITE_PSNR, INFINITE_PSNR, INFINITE_PSNR, INFINITE_PSNR);

	/**
	 * Process functions.
	 */
	template<typename TYPE>
	using ProcessFn = Core::Function<void(TYPE*, const TYPE*)>;

	/**
	 * Quality for conversion.
	 */
	enum class ConvertQuality
	{
		VERY_HIGH,
		HIGH,
		MEDIUM,
		LOW,
		VERY_LOW
	};

	/**
	 * Convert @a input image to @a outFormat. 
	 * @a output will be created if not provided.
	 * @return success.
	 */
	IMAGE_DLL bool Convert(
	    Image& output, const Image& input, ImageFormat outFormat, ConvertQuality quality = ConvertQuality::MEDIUM);

	/**
	 * Gamma to Linear conversion.
	 * @a output will be created if not provided.
	 * @param output Output image.
	 * @param input Input image.
	 * @pre @a input format is R32G32B32A32_FLOAT.
	 * @return success.
	 */
	IMAGE_DLL bool GammaToLinear(Image& output, const Image& input);

	/**
	 * Linear to Gamma conversion.
	 * @a output will be created if not provided.
	 * @param output Output image.
	 * @param input Input image.
	 * @pre @a input format is R32G32B32A32_FLOAT.
	 * @return success.
	 */
	IMAGE_DLL bool LinearToGamma(Image& output, const Image& input);

	/**
	 * Generate mips.
	 * @a output will be created if not provided.
	 * @param output Output image.
	 * @param input Input image.
	 * @pre @a input format is R32G32B32A32_FLOAT.
	 * @return success.
	 */
	IMAGE_DLL bool GenerateMips(Image& output, const Image& input);

	/**
	 * Calculate PSNR.
	 * @param base Base image.
	 * @param compare Image to compare.
	 * @return Per channel PSNR in dB
	 */
	IMAGE_DLL RGBAColor CalculatePSNR(const Image& base, const Image& compare);

	/**
	 * Process Texels.
	 * @a output will be created if not provided.
	 * @param output Output image.
	 * @param input Input image.
	 * @param processFn Function to call to process texels. This will be 1 block.
	 * @pre @a output and @a input formats match.
	 * @return success.
	 */
	template<typename TYPE>
	bool ProcessTexels(Image& output, const Image& input, ProcessFn<TYPE> processFn)
	{
		return ProcessTexelsCommon(
		    output, input, [&processFn](void* o, const void* i) { processFn((TYPE*)o, (const TYPE*)i); });
	}

	IMAGE_DLL bool ProcessTexelsCommon(Image& output, const Image& input, ProcessFn<void> processFn);


} // namespace Image
