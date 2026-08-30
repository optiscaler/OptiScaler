#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer MagnifierCB : register(b0)
#endif
{
    float2 Resolution; // Screen or texture resolution in pixels
    float2 CursorPos; // Cursor position in pixel coordinates
    float2 Offset; // Offset of the lens relative to the cursor (in pixels)
    float Radius; // Radius of the magnifying glass (in pixels)
    int ZoomFactor; // Integer scaling multiplier (e.g., 2, 3, 4)
    float BorderThickness; // Thickness of the black border (in pixels)
    float3 Padding; // Padding to maintain 16-byte alignment
};

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> InTexture : register(t0);

#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
RWTexture2D<float4> OutTexture : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // 1. Boundary Guard: Ensure threads outside the texture dimensions do not execute
    if (dispatchThreadId.x >= (uint) Resolution.x || dispatchThreadId.y >= (uint) Resolution.y)
    {
        return;
    }

    // 2. Determine the current pixel coordinate
    int2 currentPixelInt = dispatchThreadId.xy;
    float2 currentPixel = float2(currentPixelInt);
    
    // 3. Calculate the center of the magnifying lens
    float2 lensCenter = CursorPos + Offset;
    
    // 4. Find the distance from the current pixel to the lens center
    float2 vecToCenter = currentPixel - lensCenter;
    float dist = length(vecToCenter);
    
    float4 finalColor;
    
    // 5. Outside the magnifier: Render the normal, unscaled image
    if (dist > Radius)
    {
        finalColor = InTexture.Load(int3(currentPixelInt, 0));
    }
    // 6. Border: Render a black outline
    else if (dist > Radius - BorderThickness)
    {
        finalColor = float4(0.0f, 0.0f, 0.0f, 1.0f); // Black border
    }
    // 7. Inside the magnifier: Apply integer scaling
    else
    {
        int maxZoom = max(1, ZoomFactor); // Prevent division by zero
        float2 scaledOffset = floor(vecToCenter / (float) maxZoom);
        
        // Calculate the source pixel coordinate around the actual cursor position
        int2 srcPixel = int2(CursorPos + scaledOffset);
        
        // Clamp coordinates to ensure we don't sample outside the texture bounds
        srcPixel = clamp(srcPixel, int2(0, 0), int2(Resolution) - int2(1, 1));
        
        finalColor = InTexture.Load(int3(srcPixel, 0));
    }
    
    // 8. Write the final calculated pixel back to the UAV
    OutTexture[currentPixelInt] = finalColor;
}