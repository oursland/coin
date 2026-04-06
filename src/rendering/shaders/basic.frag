#version 450 core

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) flat in uint fragInstanceID;

layout(location = 0) out vec4 outColor;

struct MaterialData {
    vec4 diffuse;
    vec4 specular;
    vec4 emissive;
};

layout(std430, set = 0, binding = 1) readonly buffer Materials {
    MaterialData materials[];
};

layout(std430, set = 0, binding = 3) readonly buffer ShapeMaterialMap {
    uint materialMap[];
};

struct LightData {
    vec4 direction;
    vec4 position;
    vec4 color;
};

layout(std430, set = 0, binding = 4) readonly buffer Lights {
    LightData lights[];
};

void main() {
    uint matIdx = materialMap[fragInstanceID];
    MaterialData mat = materials[matIdx];

    // Basic faceted normal from world positions
    vec3 dx = dFdx(fragWorldPos);
    vec3 dy = dFdy(fragWorldPos);
    vec3 normal = normalize(cross(dx, dy));

    vec3 viewDir = normalize(-fragWorldPos); // Assumes camera is at origin in view space, but this is world space. Just a fallback approximation.
    
    vec3 finalColor = mat.emissive.rgb;
    
    // Evaluate lighting
    for (int i = 0; i < lights.length(); i++) {
        // Assume directional light for the basic MVP (w = 0)
        vec3 lightDir = normalize(-lights[i].direction.xyz);
        
        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = diff * lights[i].color.rgb * mat.diffuse.rgb;
        
        vec3 reflectDir = reflect(-lightDir, normal);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = spec * lights[i].color.rgb * mat.specular.rgb;
        
        finalColor += (diffuse + specular) * lights[i].color.a; // alpha as "on" flag
    }

    outColor = vec4(finalColor, mat.diffuse.a);
}
