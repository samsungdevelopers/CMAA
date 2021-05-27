#version 450
/* Copyright (c) 2021, Samsung
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

precision highp float;
precision mediump int;

layout (location = 0) in vec2 inUV;

layout (set = 0, binding = 2) uniform sampler2D samplerTexture;

layout (set = 0, binding = 0) uniform block
{
	vec2 invScreen;
}ubo;

layout (location = 0) out vec4 fragColour;

const float EDGE_THRESHOLD_MIN = 0.12, EDGE_THRESHOLD_MAX = 0.125, SUBPIXEL_QUALITY = 0.75;
const int ITERATIONS = 12;
const float QUALITY[12] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0};

float rgb2luma(vec3 rgb){
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main()
{	
#ifdef FXAA_ON
	vec2 uv = inUV;
	vec3 colourCentre = texture(samplerTexture, uv).xyz;
	
	// Luma at the current fragment
	float lumaCentre = rgb2luma(colourCentre);
	
	//Lumas at the neighbouring fragments
	float lumaDown = rgb2luma(textureOffset(samplerTexture, uv, ivec2(0,-1)).rgb);
	float lumaUp = rgb2luma(textureOffset(samplerTexture, uv, ivec2(0,1)).rgb);
	float lumaLeft = rgb2luma(textureOffset(samplerTexture, uv, ivec2(-1,0)).rgb);
	float lumaRight = rgb2luma(textureOffset(samplerTexture, uv, ivec2(1,0)).rgb);
	
	// Find the max and min luma around the current fragment
	float lumaMin = min(lumaCentre,min(min(lumaDown,lumaUp),min(lumaLeft,lumaRight)));
	float lumaMax = max(lumaCentre,max(max(lumaDown,lumaUp),max(lumaLeft,lumaRight)));
	
	// Compute the delta.
	float lumaRange = lumaMax - lumaMin;
	
	// If the luma variation is lower that a threshold (or if we are in a really dark area), we are not on an edge, don't perform any AA.
	if(lumaRange < EDGE_THRESHOLD_MIN){
		fragColour = vec4(colourCentre.rgb, 1.0);
	} else {
		// Query the 4 remaining corners lumas.
		float lumaDownLeft = rgb2luma(textureOffset(samplerTexture, uv, ivec2(-1,-1)).rgb);
		float lumaUpRight = rgb2luma(textureOffset(samplerTexture, uv, ivec2(1,1)).rgb);
		float lumaUpLeft = rgb2luma(textureOffset(samplerTexture, uv, ivec2(-1,1)).rgb);
		float lumaDownRight = rgb2luma(textureOffset(samplerTexture, uv, ivec2(1,-1)).rgb);
		
		// Combine the four edges lumas (using intermediary variables for future computations with the same values).
		float lumaDownUp = lumaDown + lumaUp;
		float lumaLeftRight = lumaLeft + lumaRight;

		// Same for corners
		float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
		float lumaDownCorners = lumaDownLeft + lumaDownRight;
		float lumaRightCorners = lumaDownRight + lumaUpRight;
		float lumaUpCorners = lumaUpRight + lumaUpLeft;
		
		// Compute an estimation of the gradient along the horizontal and vertical axis.
		float edgeHorizontal =  abs(-2.0 * lumaLeft + lumaLeftCorners)  + abs(-2.0 * lumaCentre + lumaDownUp ) * 2.0    + abs(-2.0 * lumaRight + lumaRightCorners);
		float edgeVertical =    abs(-2.0 * lumaUp + lumaUpCorners)      + abs(-2.0 * lumaCentre + lumaLeftRight) * 2.0  + abs(-2.0 * lumaDown + lumaDownCorners);
		
		// Is the local edge horizontal or vertical?
		bool isHorizontal = (edgeHorizontal >= edgeVertical);
		
		// Select the two neighboring texels lumas in the opposite direction to the local edge.
		float luma1 = isHorizontal ? lumaDown : lumaLeft;
		float luma2 = isHorizontal ? lumaUp : lumaRight;
		// Compute gradients in this direction.
		float gradient1 = luma1 - lumaCentre;
		float gradient2 = luma2 - lumaCentre;
		
		// Which direction is the steepest ?
		bool is1Steepest = abs(gradient1) >= abs(gradient2);

		// Gradient in the corresponding direction, normalized.
		float gradientScaled = 0.25*max(abs(gradient1),abs(gradient2));
		
		// Choose the step size (one pixel) according to the edge direction.
		float stepLength = isHorizontal ? ubo.invScreen.y : ubo.invScreen.x;

		// Average luma in the correct direction.
		float lumaLocalAverage = 0.0;
		
		if(is1Steepest){
			// Switch the direction
			stepLength = -stepLength;
			lumaLocalAverage = 0.5*(luma1 + lumaCentre);
		} else {
			lumaLocalAverage = 0.5*(luma2 + lumaCentre);
		}
		
		// Shift UV in the correct direction by half a pixel.
		vec2 currentUv = uv;
		if(isHorizontal)
			currentUv.y += stepLength * 0.5;
		else 
			currentUv.x += stepLength * 0.5;
		
		// Compute offset (for each iteration step) in the right direction.
		vec2 offset = isHorizontal ? vec2(ubo.invScreen.x,0.0) : vec2(0.0,ubo.invScreen.y);
		// Compute UVs to explore on each side of the edge, orthogonally. The QUALITY allows us to step faster.
		vec2 uv1 = currentUv - offset;
		vec2 uv2 = currentUv + offset;

		// Read the lumas at both current extremities of the exploration segment, and compute the delta wrt to the local average luma.
		float lumaEnd1 = rgb2luma(texture(samplerTexture,uv1).rgb);
		float lumaEnd2 = rgb2luma(texture(samplerTexture,uv2).rgb);
		lumaEnd1 -= lumaLocalAverage;
		lumaEnd2 -= lumaLocalAverage;
		
		// If the luma deltas at the current extremities are larger than the local gradient, we have reached the side of the edge.
		bool reached1 = abs(lumaEnd1) >= gradientScaled;
		bool reached2 = abs(lumaEnd2) >= gradientScaled;
		bool reachedBoth = reached1 && reached2;
		
		// If the side is not reached, we continue to explore in this direction.
		if(!reached1){
			uv1 -= offset;
		}
		if(!reached2){
			uv2 += offset;
		}   
		
		// If both sides have not been reached, continue to explore.
		if(!reachedBoth){

			for(int i = 2; i < ITERATIONS; i++){
				// If needed, read luma in 1st direction, compute delta.
				if(!reached1){
					lumaEnd1 = rgb2luma(texture(samplerTexture, uv1).rgb);
					lumaEnd1 = lumaEnd1 - lumaLocalAverage;
				}
				// If needed, read luma in opposite direction, compute delta.
				if(!reached2){
					lumaEnd2 = rgb2luma(texture(samplerTexture, uv2).rgb);
					lumaEnd2 = lumaEnd2 - lumaLocalAverage;
				}
				// If the luma deltas at the current extremities is larger than the local gradient, we have reached the side of the edge.
				reached1 = abs(lumaEnd1) >= gradientScaled;
				reached2 = abs(lumaEnd2) >= gradientScaled;
				reachedBoth = reached1 && reached2;

				// If the side is not reached, we continue to explore in this direction, with a variable quality.
				if(!reached1){
					uv1 -= offset * QUALITY[i];
				}
				if(!reached2){
					uv2 += offset * QUALITY[i];
				}

				// If both sides have been reached, stop the exploration.
				if(reachedBoth) break;
			}
		}
		// Compute the distances to each extremity of the edge.
		float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
		float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);

		// In which direction is the extremity of the edge closer ?
		bool isDirection1 = distance1 < distance2;
		float distanceFinal = min(distance1, distance2);

		// Length of the edge.
		float edgeThickness = (distance1 + distance2);

		// UV offset: read in the direction of the closest side of the edge.
		float pixelOffset = -distanceFinal / edgeThickness + 0.5;
		
		// Is the luma at center smaller than the local average ?
		bool isLumaCenterSmaller = lumaCentre < lumaLocalAverage;

		// If the luma at center is smaller than at its neighbour, the delta luma at each end should be positive (same variation).
		// (in the direction of the closer side of the edge.)
		bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;

		// If the luma variation is incorrect, do not offset.
		float finalOffset = correctVariation ? pixelOffset : 0.0;
		
		// Sub-pixel shifting
		// Full weighted average of the luma over the 3x3 neighborhood.
		float lumaAverage = (1.0/12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
		// Ratio of the delta between the global average and the center luma, over the luma range in the 3x3 neighborhood.
		float subPixelOffset1 = clamp(abs(lumaAverage - lumaCentre)/lumaRange,0.0,1.0);
		float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
		// Compute a sub-pixel offset based on this delta.
		float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;

		// Pick the biggest of the two offsets.
		finalOffset = max(finalOffset,subPixelOffsetFinal);
		
		// Compute the final UV coordinates.
		vec2 finalUv = uv;
		if(isHorizontal){
			finalUv.y += finalOffset * stepLength;
		} else {
			finalUv.x += finalOffset * stepLength;
		}

		// Read the color at the new UV coordinates, and use it.
		fragColour = texture(samplerTexture, finalUv);
	}
#else
	fragColour = texture(samplerTexture, inUV);
#endif
}