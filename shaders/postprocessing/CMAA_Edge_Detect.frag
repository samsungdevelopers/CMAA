#version 460

precision mediump float;
precision mediump int;

layout (set = 0, binding = 1) uniform sampler2D inputSceneTexture;

restrict layout (set = 0, binding = 2) writeonly uniform image2D outputSceneImage;

restrict layout (set = 0, binding = 3) buffer threadCountBuffer
{
	highp uint numCandidates;
};
restrict layout (set = 0, binding = 4) writeonly buffer candidatePosBuffer
{
	highp uint candidatePos[];
};

layout (location = 0) out vec4 outEdges;

float EdgeDetectColorCalcDiff( vec3 colorA, vec3 colorB )
{
    const vec3 cLumaConsts = vec3(0.299, 0.587, 0.114);                     // this matches FXAA (http://en.wikipedia.org/wiki/CCIR_601); above code uses http://en.wikipedia.org/wiki/Rec._709 
    return abs( dot( colorA, cLumaConsts ) - dot( colorB, cLumaConsts ) );
}

void main()
{
	vec2 temp = gl_FragCoord.xy; // this is needed to workaround a Mali compiler crash
	ivec2 screenPosI = ivec2(temp) * 2;

	float storeFlagFrag00 = 0;
	float storeFlagFrag10 = 0;
    float storeFlagFrag20 = 0;
    float storeFlagFrag01 = 0;
    float storeFlagFrag11 = 0;
    float storeFlagFrag21 = 0;
    float storeFlagFrag02 = 0;
    float storeFlagFrag12 = 0;

    vec2 et;
	const float colourThreshold = 0.08;
	
	vec3 frag00 = texelFetch(inputSceneTexture, screenPosI, 0).rgb;
	vec3 frag10 = texelFetch(inputSceneTexture, screenPosI + ivec2(1,0), 0).rgb;
	vec3 frag01 = texelFetch(inputSceneTexture, screenPosI + ivec2(0,1), 0).rgb;
	vec3 frag20 = texelFetch(inputSceneTexture, screenPosI + ivec2(2,0), 0).rgb;
	vec3 frag11 = texelFetch(inputSceneTexture, screenPosI + ivec2(1,1), 0).rgb;
	vec3 frag02 = texelFetch(inputSceneTexture, screenPosI + ivec2(0,2), 0).rgb;
	vec3 frag21 = texelFetch(inputSceneTexture, screenPosI + ivec2(2,1), 0).rgb;
	vec3 frag12 = texelFetch(inputSceneTexture, screenPosI + ivec2(1,2), 0).rgb;

	{
        et.x = EdgeDetectColorCalcDiff( frag00, frag10 );
        et.y = EdgeDetectColorCalcDiff( frag00, frag01 );
        et = clamp( et - colourThreshold, 0.0, 1.0 );
        uvec2 eti = uvec2( et * 15 + 0.99 );
        outEdges.x = float(eti.x | (eti.y << 4)) / 255.0;

		storeFlagFrag00 += et.x;
        storeFlagFrag00 += et.y;
        storeFlagFrag10 += et.x;
        storeFlagFrag01 += et.y;
    }
		
    {
        et.x = EdgeDetectColorCalcDiff( frag10, frag20 );
        et.y = EdgeDetectColorCalcDiff( frag10, frag11 );
        et = clamp( et - colourThreshold, 0.0, 1.0 );
        uvec2 eti = uvec2( et * 15 + 0.99 );
        outEdges.y = float(eti.x | (eti.y << 4)) / 255.0;

        storeFlagFrag10 += et.x;
        storeFlagFrag10 += et.y;
        storeFlagFrag20 += et.x;
        storeFlagFrag11 += et.y;    
	}
	
    {
        et.x = EdgeDetectColorCalcDiff( frag01, frag11 );
        et.y = EdgeDetectColorCalcDiff( frag01, frag02 );
        et = clamp( et - colourThreshold, 0.0, 1.0 );
        uvec2 eti = uvec2( et * 15 + 0.99 );
        outEdges.z = float(eti.x | (eti.y << 4)) / 255.0;

        storeFlagFrag01 += et.x;
        storeFlagFrag01 += et.y;
        storeFlagFrag11 += et.x;
        storeFlagFrag02 += et.y;    
	}
	
    {
        et.x = EdgeDetectColorCalcDiff( frag11, frag21 );
        et.y = EdgeDetectColorCalcDiff( frag11, frag12 );
        et = clamp( et - colourThreshold, 0.0, 1.0 );
        uvec2 eti = uvec2( et * 15 + 0.99 );
        outEdges.w = float(eti.x | (eti.y << 4)) / 255.0;

        storeFlagFrag11 += et.x;
        storeFlagFrag11 += et.y;
        storeFlagFrag21 += et.x;
        storeFlagFrag12 += et.y;
    }
	
	imageStore(outputSceneImage, screenPosI.xy + ivec2( 0, 0 ), vec4(frag00, 1.0));
	imageStore(outputSceneImage, screenPosI.xy + ivec2( 1, 0 ), vec4(frag10, 1.0));
	imageStore(outputSceneImage, screenPosI.xy + ivec2( 0, 1 ), vec4(frag01, 1.0));
	imageStore(outputSceneImage, screenPosI.xy + ivec2( 1, 1 ), vec4(frag11, 1.0));
	
	if(any(bvec4(outEdges)))
    {		
		highp ivec2 screenPosIBase = screenPosI.xy/2;
		uint index = atomicAdd(numCandidates, 1);
		candidatePos[index] = uint(screenPosIBase.x << 16 | screenPosIBase.y);
	}
}