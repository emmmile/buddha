const sampler_t samp = CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST | CLK_NORMALIZED_COORDS_FALSE;

__constant float inv = 1.0f / 255.0f;

__kernel void convertImage ( __read_only image2d_t src, __write_only image2d_t dst,
			     __read_only float c, __read_only float rmul,
			     __read_only float gmul, __read_only float bmul ) {
	// take the position in the index space
	int2 pos = (int2) ( get_global_id(0), get_global_id(1) );
	// read the pixel and convert it to float
	float4 pixel = convert_float4( read_imageui( src, samp, pos ) );
	// compute the necessary operations
	pixel = (float4)( fmin( pow( pixel.x, c ) * rmul, 255.0f ) * inv,
			  fmin( pow( pixel.y, c ) * gmul, 255.0f ) * inv,
			  fmin( pow( pixel.z, c ) * bmul, 255.0f ) * inv,
			  1.0 );
	// write it on the destination buffer
	write_imagef( dst, pos, pixel );
}

//r = min( powf( raw[j + 0], realContrast ) * rmul, 255.0f );
//g = min( powf( raw[j + 1], realContrast ) * gmul, 255.0f );
//b = min( powf( raw[j + 2], realContrast ) * bmul, 255.0f );
