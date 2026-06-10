// Scene Bindings (Set 1)
#define eCamera   0  // Access to the object descriptions
#define eGlobals  12  // Global uniform containing camera matrices
#define eObjDescs 13  // Access to the object descriptions
#define eTextures 14  // Access to textures

// Rtx Bindings (Set 0)
#define eTlas 17  // Top-level acceleration structure
#define eOutImage 16   // Ray tracer output image



// Information of a obj model when referenced in a shader
struct ObjDesc
{
    uint64_t vertexBufferAddress;
    uint64_t indexBufferAddress;
    uint64_t materialBufferAddress;
    uint64_t materialIndexBufferAddress;

    uint  firstIndex;
    int   baseVertex;
    uint  textureArrayOffset;
    uint  reserved;
};

// Uniform buffer set at each frame
struct GlobalUniforms
{
  mat4 viewProj;     // Camera view * projection
  mat4 viewInverse;  // Camera inverse view matrix
  mat4 projInverse;  // Camera inverse projection matrix
};

// Push constant structure for the raster
struct PushConstantRaster
{
  mat4  modelMatrix;  // matrix of the instance
  vec3  lightPosition;
  uint  objIndex;
  float lightIntensity;
  int   lightType;
};


// Push constant structure for the ray tracer
struct PushConstantRay
{
  vec4  clearColor;
  vec3  lightPosition;
  float lightIntensity;
  int   lightType;
};
