#version 450 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform FilterParams {
    int filterType;       // 0=None, 1=Grayscale, 2=GaussianBlur, 3=SobelEdge, 4=Invert, 5=Sepia, 6=Sharpen, 7=Emboss
    float brightness;     // -1.0 to 1.0
    float contrast;       // 0.0 to 2.0
    float saturation;     // 0.0 to 2.0
    float intensity;      // filter strength
    float sigma;          // for gaussian blur
} params;

// Helper: clamp color to [0,1]
vec3 clampColor(vec3 color) {
    return clamp(color, vec3(0.0), vec3(1.0));
}

// BT.709 luminance
float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Apply brightness/contrast/saturation
vec3 applyAdjustments(vec3 color) {
    // Brightness
    color += params.brightness;
    
    // Contrast
    color = (color - 0.5) * params.contrast + 0.5;
    
    // Saturation
    float gray = luminance(color);
    color = mix(vec3(gray), color, params.saturation);
    
    return clampColor(color);
}

// 1. Grayscale filter
vec3 filterGrayscale(vec3 color) {
    float gray = luminance(color);
    return vec3(gray);
}

// 2. Invert filter
vec3 filterInvert(vec3 color) {
    return vec3(1.0) - color;
}

// 3. Sepia filter
vec3 filterSepia(vec3 color) {
    mat3 sepiaMatrix = mat3(
        0.393, 0.349, 0.272,
        0.769, 0.686, 0.534,
        0.189, 0.168, 0.131
    );
    return clampColor(color * sepiaMatrix);
}

// 4. Sharpen filter (3x3 Laplacian kernel)
vec3 filterSharpen(vec2 texCoord) {
    vec2 texelSize = 1.0 / textureSize(uTexture, 0);
    
    vec3 center = texture(uTexture, texCoord).rgb;
    vec3 top = texture(uTexture, texCoord + vec2(0, -texelSize.y)).rgb;
    vec3 bottom = texture(uTexture, texCoord + vec2(0, texelSize.y)).rgb;
    vec3 left = texture(uTexture, texCoord + vec2(-texelSize.x, 0)).rgb;
    vec3 right = texture(uTexture, texCoord + vec2(texelSize.x, 0)).rgb;
    
    // Kernel: [ 0, -1,  0]
    //         [-1,  5, -1]
    //         [ 0, -1,  0]
    vec3 result = center * 5.0 - top - bottom - left - right;
    return clampColor(result);
}

// 5. Emboss filter
vec3 filterEmboss(vec2 texCoord) {
    vec2 texelSize = 1.0 / textureSize(uTexture, 0);
    
    vec3 tl = texture(uTexture, texCoord + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 br = texture(uTexture, texCoord + vec2(texelSize.x, texelSize.y)).rgb;
    
    vec3 result = (br - tl) + 0.5;
    return clampColor(result);
}

// 6. Gaussian Blur (5x5 separable approximation)
vec3 filterGaussianBlur(vec2 texCoord) {
    vec2 texelSize = 1.0 / textureSize(uTexture, 0);
    
    // 5x5 Gaussian kernel weights (sigma ~ 1.0)
    float weights[5] = float[](0.06136, 0.24477, 0.38774, 0.24477, 0.06136);
    float offsets[5] = float[](-2.0, -1.0, 0.0, 1.0, 2.0);
    
    vec3 result = vec3(0.0);
    
    // Horizontal pass
    for (int i = 0; i < 5; i++) {
        vec2 offset = vec2(offsets[i] * texelSize.x, 0.0);
        result += texture(uTexture, texCoord + offset).rgb * weights[i];
    }
    
    // Vertical pass (for simplicity, we do both in one pass here,
    // in production you'd do two passes for performance)
    vec3 resultV = vec3(0.0);
    for (int i = 0; i < 5; i++) {
        vec2 offset = vec2(0.0, offsets[i] * texelSize.y);
        // Note: this is approximate since we're sampling from the already blurred horizontal result
        // For true separable blur we'd need two passes
        resultV += texture(uTexture, texCoord + offset).rgb * weights[i];
    }
    
    // Average both passes approximation
    return clampColor((result + resultV) * 0.5);
}

// 7. Sobel Edge Detection
vec3 filterSobelEdge(vec2 texCoord) {
    vec2 texelSize = 1.0 / textureSize(uTexture, 0);
    
    // Get 3x3 neighborhood
    float tl = luminance(texture(uTexture, texCoord + vec2(-texelSize.x, -texelSize.y)).rgb);
    float t  = luminance(texture(uTexture, texCoord + vec2(0.0, -texelSize.y)).rgb);
    float tr = luminance(texture(uTexture, texCoord + vec2(texelSize.x, -texelSize.y)).rgb);
    float l  = luminance(texture(uTexture, texCoord + vec2(-texelSize.x, 0.0)).rgb);
    float r  = luminance(texture(uTexture, texCoord + vec2(texelSize.x, 0.0)).rgb);
    float bl = luminance(texture(uTexture, texCoord + vec2(-texelSize.x, texelSize.y)).rgb);
    float b  = luminance(texture(uTexture, texCoord + vec2(0.0, texelSize.y)).rgb);
    float br = luminance(texture(uTexture, texCoord + vec2(texelSize.x, texelSize.y)).rgb);
    
    // Sobel kernels
    // Gx: [-1, 0, 1]
    //     [-2, 0, 2]
    //     [-1, 0, 1]
    float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;
    
    // Gy: [-1, -2, -1]
    //     [ 0,  0,  0]
    //     [ 1,  2,  1]
    float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
    
    // Edge magnitude
    float magnitude = sqrt(gx*gx + gy*gy);
    magnitude = clamp(magnitude * params.intensity, 0.0, 1.0);
    
    return vec3(magnitude);
}

void main() {
    vec4 texColor = texture(uTexture, fragTexCoord);
    vec3 color = texColor.rgb;
    
    // Apply selected filter
    switch(params.filterType) {
        case 1: // Grayscale
            color = filterGrayscale(color);
            break;
        case 2: // Gaussian Blur
            color = filterGaussianBlur(fragTexCoord);
            break;
        case 3: // Sobel Edge
            color = filterSobelEdge(fragTexCoord);
            break;
        case 4: // Invert
            color = filterInvert(color);
            break;
        case 5: // Sepia
            color = filterSepia(color);
            break;
        case 6: // Sharpen
            color = filterSharpen(fragTexCoord);
            break;
        case 7: // Emboss
            color = filterEmboss(fragTexCoord);
            break;
        default: // None (0)
            // No filter
            break;
    }
    
    // Apply brightness/contrast/saturation adjustments
    color = applyAdjustments(color);
    
    outColor = vec4(color, texColor.a);
}
