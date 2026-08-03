#version 430
/**
 * The shader marks the volume cells according to the volume dimension. 
 * Each cell containing some triangles contains z position of front facing triangle in a lowest byte
 * and z position of back facing triangle in 2nd lowest byte. 
 */

layout(std430, binding = 0) buffer ZBuffer
{
    uint data[];
};

uniform ivec3 volumeDim;
layout(location=0) out vec4 outputColor;

in vec3 position;

void main()
{
    // convert the position in cell to byte
    uint zcell = uint((position.z * volumeDim.z - uint(position.z * volumeDim.z))*16);

    // compute cell index
    uint x = uint(position.x * volumeDim.x);
    uint y = uint(position.y * volumeDim.y);
    uint z = uint(position.z * volumeDim.z);
    uint offset = (x * volumeDim.y + y) * volumeDim.z + z;

    atomicOr(data[offset], 1 << (zcell + (gl_FrontFacing?0:16)));
    outputColor = vec4(1, (gl_FrontFacing?1:0), 0, 1.0);
}