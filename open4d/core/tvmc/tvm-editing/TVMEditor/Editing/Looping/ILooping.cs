using System.Numerics;
using TVMEditor.Structures;

namespace TVMEditor.Editing.Looping
{
    public interface ILooping
    {
        DualQuaternion[] ComputeFirstFrameCentersTransformations(Vector3[][] centers);
        DualQuaternion[] ComputeLastFrameCentersTransformations(Vector3[][] centers);
    }
}
